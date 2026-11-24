//===--- SemaSafeC.cpp - Safe C (-fsafe-c) regions and diagnostics -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// `__unsafe` region tracking, and Safe C diagnostics registered at run time.
// New Safe C errors go in the table below rather than in
// DiagnosticSemaKinds.td: editing the .td regenerates tables most of Sema
// includes, while this file is compiled on its own.
//
//===----------------------------------------------------------------------===//

#include "SafeCState.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/Analyses/LifetimeSafety/LifetimeAnnotations.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"
#include <functional>

using namespace clang;

SafeCState &Sema::safeC() {
  if (!SafeC)
    SafeC = std::make_unique<SafeCState>();
  return *SafeC;
}

bool Sema::inSafeCUnsafeParse() const { return SafeC && SafeC->Depth > 0; }

bool Sema::isInSafeCUnsafeRegion(SourceLocation Loc) const {
  if (Loc.isInvalid() || !SafeC || SafeC->Regions.empty())
    return false;
  Loc = SourceMgr.getFileLoc(Loc);
  for (const SafeCState::Region &R : SafeC->Regions) {
    SourceLocation B = SourceMgr.getFileLoc(R.Range.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.Range.getEnd());
    if (!SourceMgr.isBeforeInTranslationUnit(Loc, B) &&
        !SourceMgr.isBeforeInTranslationUnit(E, Loc))
      return true;
  }
  return false;
}

void Sema::beginSafeCUnsafeRegion(SourceLocation Begin, bool IsFunction) {
  SafeCState &St = safeC();
  ++St.Depth;
  St.Open.push_back(St.Regions.size());
  St.Regions.push_back({SourceRange(Begin, Begin), IsFunction});
}

void Sema::endSafeCUnsafeRegion(SourceLocation End) {
  SafeCState &St = safeC();
  if (St.Depth == 0 || St.Open.empty())
    return;
  --St.Depth;
  SafeCState::Region &R = St.Regions[St.Open.pop_back_val()];
  R.Range.setEnd(End.isValid() ? End : R.Range.getBegin());
}

void Sema::noteSafeCUnsafeRegionUsed(SourceLocation Loc) {
  SafeCState &St = safeC();
  if (!St.Open.empty()) {
    St.Regions[St.Open.back()].Used = true;
    return;
  }
  // Raised after parsing: the innermost closed region holding Loc is the
  // latest-starting one that contains it.
  Loc = SourceMgr.getFileLoc(Loc);
  SafeCState::Region *Best = nullptr;
  for (SafeCState::Region &R : St.Regions) {
    SourceLocation B = SourceMgr.getFileLoc(R.Range.getBegin());
    SourceLocation E = SourceMgr.getFileLoc(R.Range.getEnd());
    if (SourceMgr.isBeforeInTranslationUnit(Loc, B) ||
        SourceMgr.isBeforeInTranslationUnit(E, Loc))
      continue;
    if (!Best || SourceMgr.isBeforeInTranslationUnit(
                     SourceMgr.getFileLoc(Best->Range.getBegin()), B))
      Best = &R;
  }
  if (Best)
    Best->Used = true;
}

void Sema::emitSafeCUnusedUnsafeWarnings() {
  // An __unsafe region in which nothing was suppressed lifts checks for no
  // reason. Only regions spelled directly in the user's code are reported: one
  // inside another macro may be needed at its other uses. After an error,
  // some diagnostics were never issued, so say nothing.
  if (!getLangOpts().SafeC || !SafeC || getDiagnostics().hasErrorOccurred())
    return;
  for (const SafeCState::Region &R : SafeC->Regions) {
    SourceLocation B = R.Range.getBegin();
    if (R.Used || B.isInvalid() || SourceMgr.isInSystemHeader(B) ||
        (B.isMacroID() && SourceMgr.getImmediateMacroCallerLoc(B).isMacroID()))
      continue;
    Diag(B, diag::warn_safe_c_unused_unsafe) << R.IsFunction;
  }
}

namespace {
/// A Safe C diagnostic registered at run time. Errors are issued as errors no
/// flag or pragma can lower, and are ignored inside `__unsafe`; notes attach
/// to the preceding error.
struct SafeCDiagInfo {
  const char *Key;
  bool IsNote;
  const char *Text;
};

// New Safe C diagnostics go here. Say why the code is unsafe and what to
// write instead; never suggest __unsafe first.
const SafeCDiagInfo SafeCDiagTable[] = {
    {"returns-fresh", false,
     "%0 returns memory it allocated, but callers are not told, so a use "
     "after they free it goes unchecked; declare it "
     "'__attribute__((ownership_returns(malloc)))' (safec.h: "
     "'_Returns_owned')"},
    {"frees-param", false,
     "%0 frees its parameter %1, but callers are not told and may use it "
     "afterwards; mark the parameter '_Owned' (it then takes ownership, like "
     "a Rust 'Box<T>' parameter)"},
    {"storage-borrow", false,
     "%select{field|global}0 %1 holds a pointer nothing tracks, so it can "
     "outlive what it points to; declare it '_Owned' if it is "
     "responsible for freeing its target, or '_Static' if it only "
     "points at string literals, globals or functions%select{, or mark the "
     "struct '_View' if it lives only in locals and parameters|}0"},
    {"static-store", false,
     "%0 is '_Static', so it may point only at string literals, "
     "globals or functions, which are never freed; this value may point at "
     "memory that is; make %0 '_Owned' if it is responsible for "
     "freeing what it points to"},
    {"unchecked-read", false,
     "%0 is '_Unchecked': nothing tracks whether its target is still "
     "alive, so its value can be used only inside '__unsafe' (or copied into "
     "another '_Unchecked'); if this struct owns the target, make %0 "
     "'_Owned' instead"},
    {"view-storage", false,
     "%select{global|field}0 %1 holds a '_View' struct, whose borrows "
     "are tracked only in locals and parameters; make it a local, or pass it "
     "as a parameter"},
    {"take-operand", false,
     "'_Take' moves a pointer out of a field or global and leaves "
     "null behind, so it needs a pointer that can be assigned, such as "
     "'_Take(n->next)'"},
    {"take-nonnull", false,
     "'_Take' leaves null behind, so '%0' must be declared "
     "'_Nullable'"},
    {"owned-store", false,
     "%0 is '_Owned', so it must be given a pointer it may free: a "
     "new allocation, an owned local (which is then moved), or "
     "'_Take(...)' of another owned field; this value is borrowed, so "
     "its owner would free it too"},
    {"free-in-place", false,
     "freeing a pointer that is still stored in %0 leaves it dangling; move "
     "it out first, as in 'free(_Take(n->next))', where the place is "
     "'_Owned'"},
    {"free-borrowed-take", false,
     "%0 is borrowed, so its owner will free what it points to too; declare "
     "it '_Owned' if this code is responsible for freeing it"},
    {"alloc-untracked", false,
     "a new allocation stored in %0 is not tracked, so nothing stops a use "
     "after it is freed; declare the place '_Owned' (for an array: "
     "'T *_Owned a[N]'), or keep the pointer in a local variable"},
    {"owning-struct-copy", false,
     "copying %0 duplicates its owned field %1, and both copies would free "
     "it; pass a pointer to it instead, or move the field out with "
     "'_Take'"},
    {"heap-slot-borrow", false,
     "this heap memory holds pointers of type %0, which nothing tracks, so "
     "they can outlive what they point to; give the inner pointer a kind, as "
     "in 'T *_Owned *' if the memory owns what they point to, or "
     "'T *_Static *'"},
    {"ownership-through-borrow", false,
     "%select{taking from|replacing}0 an owned field through '%1' changes "
     "what its owner holds while callers may still be using it; declare '%1' "
     "'_Excl' (an exclusive borrow, like Rust's '&mut')%select{, or "
     "'_Owned' if this function owns it|}2"},
    {"excl-reassigned", false,
     "%0 is an exclusive borrow of the caller's object, which the caller "
     "still owns after the call; %select{assigning a new pointer to it|"
     "giving it away}1 would leave the caller with the wrong object; use a "
     "local variable instead"},
    {"excl-aliased-arg", false,
     "%0 is lent exclusively to this call, so no other argument may reach "
     "it (the callee could free or replace what that argument points to); "
     "pass it once, or finish with the other use before the call"},
    {"return-needs-lifetimebound", false,
     "%0 returns a pointer borrowed from %1, so it is valid only as long as what %1 "
     "points to; mark the parameter '_Lifetimebound' so callers keep "
     "it alive, or return an owned copy"},
    {"excl-arg-borrowed", false,
     "%0 is a shared borrow, but this parameter is '_Excl': the callee may "
     "free or replace what it owns while the owner still uses it; pass the "
     "owner, or declare %0 '_Owned' (if it owns the object) or '_Excl'"},
    {"drop-moved-by-name", false,
     "%0 is freed automatically at the end of its scope ('_Drop'), so giving "
     "it away here would free it twice; move it out with '_Take(%1)', which "
     "leaves null for the cleanup"},
    {"owning-struct-byte-copy", false,
     "copying %0 byte by byte duplicates its owned field %1, and both copies "
     "would free it; move the field with '_Take' and assign the others, or "
     "pass a pointer to the struct instead"},
    {"tag-field-missing", false,
     "the union is tagged by '%0', but the struct has no field of that name; "
     "name the field that holds the tag, as in '_Tagged_by(kind)'"},
    {"tag-field-type", false,
     "the tag '%0' must be an integer or enum field, so its value can say "
     "which member is live"},
    {"when-missing", false,
     "member %0 of a tagged union does not say when it is live; add "
     "'_When(VALUE)' with the tag values for which it holds data"},
    {"when-duplicate", false,
     "tag value %0 is claimed by both %1 and %2; each value must name one "
     "live member"},
    {"switch-not-exhaustive", false,
     "this switch on '%0' does not handle %1, a tag value of the union it "
     "describes; add 'case %1:' (or 'default:')"},
    {"union-member-unchecked", false,
     "%select{reading|writing}0 %1 needs the tag '%2' known to be %3 here, "
     "or the union's bytes could be another member's; check it first (as in "
     "'if (x.%2 == %3)' or 'case %3:')%select{|, or set it ('x.%2 = %3;') "
     "before writing}0"},
    {"field-borrow-ended", false,
     "this pointer is borrowed from '%0', and the borrow ends (the field is "
     "taken, replaced, or lent to an exclusive parameter) before its last "
     "use; read '%0' again after that point, or finish using the pointer "
     "first"},
    {"field-borrow-ended-here", true, "the borrow ends here"},
    {"reborrow-ended", false,
     "%0 returns an exclusive borrow of '%1', which ends when '%1' is lent "
     "or given away again, before this pointer's last use; call %0 again "
     "after that point, or finish using the pointer first"},
    {"view-write-through-pointer", false,
     "writing a borrow into a '_View' through a pointer can outlive "
     "what it points to, because the view's own lifetime is unknown here; "
     "write the field of a local view by name ('v.%0 = ...')"},
};
} // namespace

Sema::SemaDiagnosticBuilder Sema::SafeCDiag(SourceLocation Loc,
                                            StringRef Key) {
  static const llvm::StringMap<const SafeCDiagInfo *> Table = [] {
    llvm::StringMap<const SafeCDiagInfo *> M;
    for (const SafeCDiagInfo &D : SafeCDiagTable)
      M[D.Key] = &D;
    return M;
  }();
  auto It = Table.find(Key);
  assert(It != Table.end() && "unknown Safe C diagnostic key");
  const SafeCDiagInfo &D = *It->second;
  DiagnosticIDs &IDs = *getDiagnostics().getDiagnosticIDs();
  if (D.IsNote)
    return Diag(Loc, IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
                         diag::Severity::Fatal, D.Text,
                         DiagnosticIDs::CLASS_NOTE)));
  bool InUnsafe = inSafeCUnsafeParse() || isInSafeCUnsafeRegion(Loc);
  if (InUnsafe)
    noteSafeCUnsafeRegionUsed(Loc);
  else
    ++NumSafeCSafetyErrors;
  return Diag(Loc, IDs.getCustomDiagID(DiagnosticIDs::CustomDiagDesc(
                       InUnsafe ? diag::Severity::Ignored
                                : diag::Severity::Error,
                       D.Text,
                       InUnsafe ? DiagnosticIDs::CLASS_WARNING
                                : DiagnosticIDs::CLASS_ERROR)));
}

bool clang::safeCOwnershipEnabled(const Sema &S) {
  return S.getLangOpts().SafeC && lifetimes::safeCOwnershipFlag();
}

StringRef clang::safeCPointerKind(QualType T) {
  return lifetimes::safeCPointerKind(T);
}

static bool hasKind(const ValueDecl *D, StringRef K) {
  return safeCPointerKind(D->getType()) == K;
}

static bool hasSafeCAnnotation(const Decl *D, StringRef Name) {
  for (const auto *A : D->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == Name)
      return true;
  return false;
}

/// A pointer type that would hold a borrow: not a function pointer (functions
/// are never freed). Arrays of pointers count; their elements are stored.
static bool holdsBorrow(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  return T->isPointerType() && !T->isFunctionPointerType();
}

/// For a pointer to pointers that would live in the heap: the inner pointer
/// type, if it has no kind (a borrow stored where nothing tracks it).
static QualType plainPointerSlot(QualType PtrT) {
  if (!PtrT->isPointerType())
    return QualType();
  QualType Inner = PtrT->getPointeeType();
  while (const ArrayType *AT = Inner->getAsArrayTypeUnsafe())
    Inner = AT->getElementType();
  if (holdsBorrow(Inner) && safeCPointerKind(Inner).empty())
    return Inner;
  return QualType();
}

static bool isViewType(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  const RecordDecl *RD = T->getAsRecordDecl();
  return RD && hasSafeCAnnotation(RD, "safec_view");
}

/// The annotation of \p D named \p Name, if any.
static const AnnotateAttr *getSafeCAnnotation(const Decl *D, StringRef Name) {
  for (const auto *A : D->specific_attrs<AnnotateAttr>())
    if (A->getAnnotation() == Name)
      return A;
  return nullptr;
}

/// The tag field name of a tagged union, or "".
static StringRef taggedByName(const RecordDecl *U) {
  if (!U || !U->isUnion())
    return "";
  const AnnotateAttr *A = getSafeCAnnotation(U, "safec_tagged_by");
  if (!A || A->args_size() == 0)
    return "";
  if (const auto *SL =
          dyn_cast<StringLiteral>(A->args_begin()[0]->IgnoreParenImpCasts()))
    return SL->getString();
  return "";
}
static StringRef taggedByName(QualType T) {
  return taggedByName(T->getAsRecordDecl());
}

/// The tag values of each member of tagged union \p U, as written in _When.
static void whenValues(
    const ASTContext &Ctx, const RecordDecl *U,
    llvm::function_ref<void(const FieldDecl *, llvm::APSInt, SourceLocation)>
        Each) {
  for (const FieldDecl *M : U->fields())
    if (const AnnotateAttr *A = getSafeCAnnotation(M, "safec_when"))
      for (const Expr *V : llvm::make_range(A->args_begin(), A->args_end()))
        if (std::optional<llvm::APSInt> I = V->getIntegerConstantExpr(Ctx))
          Each(M, *I, V->getExprLoc());
}

/// M4: each tagged union member of \p RD names a sibling integer tag, and
/// each of its members says, with distinct values, when it is live.
static void checkTaggedUnions(Sema &S, const RecordDecl *RD) {
  for (const FieldDecl *FD : RD->fields()) {
    StringRef Tag = taggedByName(FD->getType());
    if (Tag.empty())
      continue;
    const FieldDecl *TagField = nullptr;
    for (const FieldDecl *F : RD->fields())
      if (F->getName() == Tag)
        TagField = F;
    if (!TagField) {
      S.SafeCDiag(FD->getLocation(), "tag-field-missing") << Tag;
      continue;
    }
    if (!TagField->getType()->isIntegralOrEnumerationType())
      S.SafeCDiag(TagField->getLocation(), "tag-field-type") << Tag;
    const RecordDecl *U = FD->getType()->getAsRecordDecl();
    for (const FieldDecl *M : U->fields())
      if (!getSafeCAnnotation(M, "safec_when"))
        S.SafeCDiag(M->getLocation(), "when-missing") << M;
    llvm::SmallVector<std::pair<llvm::APSInt, const FieldDecl *>, 8> Seen;
    whenValues(S.Context, U,
               [&](const FieldDecl *M, llvm::APSInt V, SourceLocation L) {
                 for (auto &[W, Owner] : Seen)
                   if (llvm::APSInt::isSameValue(W, V) && Owner != M)
                     S.SafeCDiag(L, "when-duplicate")
                         << toString(V, 10) << Owner << M;
                 Seen.push_back({V, M});
               });
  }
}

void clang::checkSafeCPointerStorage(Sema &S, Decl *D) {
  if (!safeCOwnershipEnabled(S) ||
      S.getSourceManager().isInSystemHeader(D->getLocation()))
    return;
  auto Kinded = [](const ValueDecl *X) {
    return !safeCPointerKind(X->getType()).empty();
  };
  if (auto *RD = dyn_cast<RecordDecl>(D)) {
    checkTaggedUnions(S, RD);
    if (hasSafeCAnnotation(RD, "safec_view"))
      return;
    for (const FieldDecl *FD : RD->fields()) {
      if (holdsBorrow(FD->getType()) && !Kinded(FD))
        S.SafeCDiag(FD->getLocation(), "storage-borrow") << 0 << FD;
      else if (QualType Slot = hasKind(FD, "owned")
                                   ? plainPointerSlot(FD->getType())
                                   : QualType();
               !Slot.isNull())
        S.SafeCDiag(FD->getLocation(), "heap-slot-borrow") << Slot;
      else if (isViewType(FD->getType()))
        S.SafeCDiag(FD->getLocation(), "view-storage") << 1 << FD;
    }
    return;
  }
  if (auto *VD = dyn_cast<VarDecl>(D);
      VD && VD->hasGlobalStorage() && !isa<ParmVarDecl>(VD)) {
    if (holdsBorrow(VD->getType()) && !Kinded(VD))
      S.SafeCDiag(VD->getLocation(), "storage-borrow") << 1 << VD;
    else if (isViewType(VD->getType()))
      S.SafeCDiag(VD->getLocation(), "view-storage") << 0 << VD;
  }
}

static bool isStaticObject(ASTContext &Ctx, const Expr *E);

/// The field or variable an lvalue names directly (x, s.f, p->f), if any.
static const ValueDecl *namedStorage(const Expr *E) {
  E = lifetimes::safeCPeel(E);
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  return nullptr;
}

/// E's value is the address of static data: a string literal, a global or
/// part of one, a function, null, or another _Static.
static bool isStaticAddress(ASTContext &Ctx, const Expr *E) {
  E = E->IgnoreParens();
  if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
    return true;
  if (const auto *CE = dyn_cast<CastExpr>(E)) {
    if (CE->getCastKind() == CK_ArrayToPointerDecay)
      return isStaticObject(Ctx, CE->getSubExpr());
    if (CE->getCastKind() == CK_FunctionToPointerDecay)
      return true;
    if (CE->getCastKind() == CK_LValueToRValue) {
      const ValueDecl *D = namedStorage(CE->getSubExpr());
      return D && hasKind(D, "static");
    }
    return isStaticAddress(Ctx, CE->getSubExpr());
  }
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    return UO->getOpcode() == UO_AddrOf && isStaticObject(Ctx, UO->getSubExpr());
  if (const auto *BO = dyn_cast<BinaryOperator>(E))
    if (BO->isAdditiveOp())
      return BO->getLHS()->getType()->isPointerType()
                 ? isStaticAddress(Ctx, BO->getLHS())
                 : isStaticAddress(Ctx, BO->getRHS());
  if (const auto *CO = dyn_cast<ConditionalOperator>(E))
    return isStaticAddress(Ctx, CO->getTrueExpr()) &&
           isStaticAddress(Ctx, CO->getFalseExpr());
  return false;
}

/// E is an object with static storage: a literal, a global, or a member or
/// element of one reached without a pointer.
static bool isStaticObject(ASTContext &Ctx, const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (isa<StringLiteral, CompoundLiteralExpr>(E))
    return isa<StringLiteral>(E) ||
           cast<CompoundLiteralExpr>(E)->isFileScope();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (isa<FunctionDecl>(DRE->getDecl()))
      return true;
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    return VD && VD->hasGlobalStorage();
  }
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return !ME->isArrow() && isStaticObject(Ctx, ME->getBase());
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
    return ASE->getBase()->IgnoreParenImpCasts()->getType()->isArrayType() &&
           isStaticObject(Ctx, ASE->getBase());
  return false;
}

/// An _Owned field, global or local, or a parameter the function
/// takes ownership of.
static bool isOwnedStorage(const ValueDecl *D) {
  if (hasKind(D, "owned"))
    return true;
  if (const auto *P = dyn_cast<ParmVarDecl>(D))
    if (const auto *F = dyn_cast<FunctionDecl>(P->getDeclContext()))
      return safeCTakesParam(F, P->getFunctionScopeIndex());
  return false;
}

/// The first owned field of \p T, looking into nested structs and arrays.
static const FieldDecl *ownedFieldOf(QualType T);
const FieldDecl *clang::safeCOwnedFieldOf(QualType T) { return ownedFieldOf(T); }
static const FieldDecl *ownedFieldOf(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  const RecordDecl *RD = T->getAsRecordDecl();
  if (!RD || !RD->getDefinition())
    return nullptr;
  for (const FieldDecl *FD : RD->getDefinition()->fields()) {
    if (hasKind(FD, "owned"))
      return FD;
    if (const FieldDecl *Inner = ownedFieldOf(FD->getType()))
      return Inner;
  }
  return nullptr;
}

/// E gives up a pointer the receiver may free: null, a new allocation, a
/// take, or a local or owned parameter (the flow layer checks it owns).
static bool isOwnedSource(ASTContext &Ctx, const Expr *E) {
  E = lifetimes::safeCPeel(E);
  if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
    return true;
  if (const auto *CE = dyn_cast<CStyleCastExpr>(E))
    return isOwnedSource(Ctx, CE->getSubExpr());
  if (const auto *Call = dyn_cast<CallExpr>(E)) {
    // Taking from a borrowed place gives a borrow; taking a local moves
    // what it holds (the lifetime analysis follows locals).
    if (Call->getBuiltinCallee() == Builtin::BI__builtin_safec_take) {
      if (Call->getNumArgs() != 1)
        return false;
      if (safeCPointerKind(Call->getArg(0)->getType()) == "owned")
        return true;
      const auto *DRE =
          dyn_cast<DeclRefExpr>(lifetimes::safeCPeel(Call->getArg(0)));
      const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
      return VD && VD->isLocalVarDecl() && !VD->isStaticLocal();
    }
    const FunctionDecl *F = Call->getDirectCallee();
    if (!F)
      return false;
    for (const auto *OA : F->specific_attrs<OwnershipAttr>())
      if (OA->isReturns())
        return true;
    return F->getIdentifier() &&
           llvm::is_contained({"malloc", "calloc", "realloc", "reallocf",
                               "strdup", "strndup", "aligned_alloc", "fopen",
                               "fdopen", "freopen", "tmpfile", "popen",
                               "opendir", "fdopendir"},
                              F->getName());
  }
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *P = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      return isOwnedStorage(P);
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    return VD && VD->isLocalVarDecl() && !VD->isStaticLocal();
  }
  return false;
}

/// A named local variable or parameter: the places the lifetime analysis
/// follows.
static bool isTrackedLocal(const Expr *E) {
  const auto *DRE = dyn_cast<DeclRefExpr>(lifetimes::safeCPeel(E));
  const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
  return VD && !VD->hasGlobalStorage();
}

/// How to name the place E stores to in a diagnostic.
static std::string describe(const Expr *E) {
  if (const ValueDecl *D = namedStorage(E))
    return "'" + D->getNameAsString() + "'";
  E = E->IgnoreParenImpCasts();
  if (isa<ArraySubscriptExpr>(E))
    return "an array element";
  return "memory reached through a pointer";
}

/// An ownership operation through \p Place is allowed when its root is a
/// stack object, or a pointer that owns it or borrows it exclusively. Reports
/// and returns false otherwise.
static bool isInferredOwnerLocal(ASTContext &Ctx, const VarDecl *VD,
                                 const Stmt *Body);

static void checkOwnershipRoot(Sema &S, const Expr *Place, unsigned Op,
                               const Stmt *Body) {
  const VarDecl *Root = lifetimes::safeCRootVar(Place);
  if (!Root || !Root->getType()->isPointerType())
    return; // a global (rejected elsewhere) or a stack object
  StringRef K = safeCPointerKind(Root->getType());
  if (K == "owned" || K == "excl" || isOwnedStorage(Root) ||
      isInferredOwnerLocal(S.Context, Root, Body))
    return;
  S.SafeCDiag(Place->getExprLoc(), "ownership-through-borrow")
      << Op << Root->getName() << (isa<ParmVarDecl>(Root) ? 1 : 0);
}

static const ValueDecl *exclusiveArgBlocker(ASTContext &Ctx, const Expr *E,
                                            const Stmt *Body);

/// A call that returns an exclusive reborrow: some `_Excl _Lifetimebound`
/// parameter gets an argument that is itself exclusive (Rust's `&mut` in,
/// `&mut` out).
static bool isExclusiveReborrowCall(ASTContext &Ctx, const Expr *E,
                                    const Stmt *Body, unsigned Depth) {
  const auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
  const FunctionDecl *F = Call ? Call->getDirectCallee() : nullptr;
  if (!F || Depth > 4)
    return false;
  for (unsigned I = 0, N = std::min(Call->getNumArgs(), F->getNumParams());
       I != N; ++I) {
    const ParmVarDecl *P = F->getParamDecl(I);
    if (safeCPointerKind(P->getType()) == "excl" &&
        P->hasAttr<LifetimeBoundAttr>() &&
        !exclusiveArgBlocker(Ctx, Call->getArg(I), Body))
      return true;
  }
  return false;
}

/// A plain local is an owner, as `let x = Box::new(..)` is in Rust, when every
/// value stored in it is owned (an allocation, an owned return, a take, or
/// null) and its address is never taken (so nothing else can store to it).
static bool isInferredOwnerLocal(ASTContext &Ctx, const VarDecl *VD,
                                 const Stmt *Body) {
  // Body is the function being checked (not yet attached to its decl).
  if (!VD->isLocalVarDecl() || VD->isStaticLocal() || !Body)
    return false;
  // Also an exclusive reborrow, when every value comes from a reborrow call.
  auto Fine = [&](const Expr *E) {
    return isOwnedSource(Ctx, E) || isExclusiveReborrowCall(Ctx, E, Body, 0);
  };
  if (const Expr *Init = VD->getInit(); Init && !Fine(Init))
    return false;
  struct Finder : RecursiveASTVisitor<Finder> {
    ASTContext &Ctx;
    const VarDecl *VD;
    std::function<bool(const Expr *)> Fine;
    bool Ok = true;
    bool names(const Expr *E) {
      const auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
      return DRE && DRE->getDecl() == VD;
    }
    bool VisitBinaryOperator(BinaryOperator *BO) {
      if (BO->isAssignmentOp() && names(BO->getLHS()) &&
          (BO->getOpcode() != BO_Assign || !Fine(BO->getRHS())))
        Ok = false;
      return Ok;
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      if (UO->getOpcode() == UO_AddrOf && names(UO->getSubExpr()))
        Ok = false;
      return Ok;
    }
  } F{{}, Ctx, VD, Fine};
  F.TraverseStmt(const_cast<Stmt *>(Body));
  return F.Ok;
}

/// Rule 8.2: an argument lent exclusively must reach its object only through
/// owners and exclusive borrows: an owned or exclusive variable, the address
/// of a stack object, or an owned field reached from one of those. Returns
/// the offending variable, or null when the argument is fine.
static const ValueDecl *exclusiveArgBlocker(ASTContext &Ctx, const Expr *E,
                                            const Stmt *Body) {
  E = lifetimes::safeCPeel(E);
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_AddrOf || UO->getOpcode() == UO_Deref)
      return exclusiveArgBlocker(Ctx, UO->getSubExpr(), Body);
  }
  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    // `p->f` passed by value is the field's pointer: it must be owned.
    if (E->getType()->isPointerType() &&
        safeCPointerKind(E->getType()) != "owned")
      return ME->getMemberDecl();
    return exclusiveArgBlocker(Ctx, ME->getBase(), Body);
  }
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    if (E->getType()->isPointerType() &&
        safeCPointerKind(E->getType()) != "owned")
      return nullptr; // an element's pointer: covered by the heap-slot rules
    return exclusiveArgBlocker(Ctx, ASE->getBase(), Body);
  }
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
  if (!VD || !VD->getType()->isPointerType())
    return nullptr; // a stack object (or array), or something else
  StringRef K = safeCPointerKind(VD->getType());
  if (K == "owned" || K == "excl" || isOwnedStorage(VD) ||
      isInferredOwnerLocal(Ctx, VD, Body))
    return nullptr;
  return VD;
}

/// A local with a cleanup (`_Drop`), named directly: giving it away by name
/// would leave the cleanup to free it again.
static const VarDecl *dropLocalNamed(const Expr *E) {
  const auto *DRE = dyn_cast<DeclRefExpr>(lifetimes::safeCPeel(E));
  const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
  return VD && VD->hasAttr<CleanupAttr>() && VD->getType()->isPointerType()
             ? VD
             : nullptr;
}

namespace {
class PointerKindChecker : public RecursiveASTVisitor<PointerKindChecker> {
  Sema &S;
  // Reads of an unchecked that are allowed: copies into another unchecked
  // and null tests. Filled by parents before their children are visited.
  llvm::SmallPtrSet<const Expr *, 8> AllowedUncheckedReads;

  void allowRead(const Expr *E) { AllowedUncheckedReads.insert(E->IgnoreParens()); }

  void checkStore(const ValueDecl *Dst, const Expr *Src, SourceLocation Loc) {
    if (!Dst)
      return;
    if (hasKind(Dst, "static") &&
        !isStaticAddress(S.Context, Src))
      S.SafeCDiag(Src->getExprLoc(), "static-store") << Dst;
    if (hasKind(Dst, "unchecked"))
      allowRead(Src);
    if (Dst->getType()->isPointerType() && isOwnedStorage(Dst) &&
        !isa<ParmVarDecl>(Dst) && !isOwnedSource(S.Context, Src))
      S.SafeCDiag(Src->getExprLoc(), "owned-store") << Dst;
    if (Dst->getType()->isPointerType() && isOwnedStorage(Dst) &&
        !isa<ParmVarDecl>(Dst))
      if (const VarDecl *D = dropLocalNamed(Src))
        S.SafeCDiag(Src->getExprLoc(), "drop-moved-by-name") << D << D->getName();
    if (const auto *Call = dyn_cast<CallExpr>(Src->IgnoreParenCasts());
        Call && isOwnedSource(S.Context, Call) &&
        Call->getBuiltinCallee() != Builtin::BI__builtin_safec_take)
      if (QualType Slot = plainPointerSlot(Dst->getType()); !Slot.isNull())
        S.SafeCDiag(Src->getExprLoc(), "heap-slot-borrow") << Slot;
  }

  // Struct copies that are moves: returning a local.
  llvm::SmallPtrSet<const Expr *, 4> AllowedStructCopies;

public:
  // The function body being checked, if any (null for global initializers).
  const Stmt *Body = nullptr;
  explicit PointerKindChecker(Sema &S, const Stmt *Body = nullptr)
      : S(S), Body(Body) {}

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->getOpcode() == BO_Assign && !isTrackedLocal(BO->getLHS()) &&
        BO->getLHS()->getType()->isPointerType() &&
        safeCPointerKind(BO->getLHS()->getType()) != "owned") {
      const auto *Call = dyn_cast<CallExpr>(BO->getRHS()->IgnoreParenCasts());
      if (Call && isOwnedSource(S.Context, Call))
        S.SafeCDiag(BO->getRHS()->getExprLoc(), "alloc-untracked")
            << describe(BO->getLHS());
    }
    if (BO->isAssignmentOp())
      if (const auto *DRE = dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParens()))
        if (safeCPointerKind(DRE->getDecl()->getType()) == "excl")
          S.SafeCDiag(BO->getOperatorLoc(), "excl-reassigned")
              << DRE->getDecl() << 0;
    if (BO->getOpcode() == BO_Assign) {
      if (!isTrackedLocal(BO->getLHS()) &&
          safeCPointerKind(BO->getLHS()->getType()) == "owned")
        checkOwnershipRoot(S, BO->getLHS(), 1, Body);
      checkStore(namedStorage(BO->getLHS()), BO->getRHS(), BO->getOperatorLoc());
      const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens());
      if (ME && ME->isArrow() && holdsBorrow(ME->getType()) &&
          isViewType(ME->getBase()->getType()->getPointeeType()))
        S.SafeCDiag(BO->getOperatorLoc(), "view-write-through-pointer")
            << ME->getMemberDecl()->getName();
    }
    if (BO->isEqualityOp()) {
      if (BO->getRHS()->isNullPointerConstant(S.Context,
                                              Expr::NPC_ValueDependentIsNotNull))
        allowRead(BO->getLHS());
      if (BO->getLHS()->isNullPointerConstant(S.Context,
                                              Expr::NPC_ValueDependentIsNotNull))
        allowRead(BO->getRHS());
    }
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (const Expr *Init = VD->getInit(); Init && !isa<InitListExpr>(Init))
      checkStore(VD, Init, VD->getLocation());
    return true;
  }

  bool VisitInitListExpr(InitListExpr *ILE) {
    // The semantic form lists every field of a struct in order.
    // The visitor sees only the syntactic form (as written); check the
    // semantic one, which has an entry per field.
    if (InitListExpr *Sem = ILE->getSemanticForm())
      ILE = Sem;
    const RecordDecl *RD = ILE->getType()->getAsRecordDecl();
    if (!RD)
      return true;
    unsigned I = 0;
    for (const FieldDecl *FD : RD->fields()) {
      if (I >= ILE->getNumInits())
        break;
      // Unnamed bit-fields get no initializer slot.
      if (FD->isUnnamedBitField())
        continue;
      if (RD->isUnion() && ILE->getInitializedFieldInUnion() != FD)
        continue;
      // A slot can be empty (filled implicitly).
      if (const Expr *Init = ILE->getInit(I++))
        checkStore(FD, Init, ILE->getBeginLoc());
    }
    return true;
  }

  bool VisitSwitchStmt(SwitchStmt *SS) {
    // M4 rule 4: a switch on a union's tag handles every tag value.
    const auto *ME = dyn_cast<MemberExpr>(SS->getCond()->IgnoreParenImpCasts());
    const auto *TagField = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
    if (!TagField)
      return true;
    llvm::SmallVector<llvm::APSInt, 8> Cases;
    for (const SwitchCase *C = SS->getSwitchCaseList(); C;
         C = C->getNextSwitchCase()) {
      if (isa<DefaultStmt>(C))
        return true;
      if (const auto *CS = dyn_cast<CaseStmt>(C))
        if (std::optional<llvm::APSInt> V =
                CS->getLHS()->getIntegerConstantExpr(S.Context))
          Cases.push_back(*V);
    }
    for (const FieldDecl *F : TagField->getParent()->fields()) {
      if (taggedByName(F->getType()) != TagField->getName())
        continue;
      whenValues(S.Context, F->getType()->getAsRecordDecl(),
                 [&](const FieldDecl *, llvm::APSInt V, SourceLocation) {
                   for (const llvm::APSInt &C : Cases)
                     if (llvm::APSInt::isSameValue(C, V))
                       return;
                   std::string Name = toString(V, 10);
                   if (const auto *ET = TagField->getType()->getAs<EnumType>())
                     for (const EnumConstantDecl *E :
                          ET->getOriginalDecl()->getDefinitionOrSelf()->enumerators())
                       if (llvm::APSInt::isSameValue(E->getInitVal(), V))
                         Name = E->getName().str();
                   S.SafeCDiag(SS->getSwitchLoc(), "switch-not-exhaustive")
                       << TagField->getName() << Name;
                 });
    }
    return true;
  }

  bool VisitCStyleCastExpr(CStyleCastExpr *CE) {
    // (void)s discards the value; nothing is copied.
    if (CE->getCastKind() == CK_ToVoid)
      AllowedStructCopies.insert(CE->getSubExpr()->IgnoreParens());
    return true;
  }

  bool VisitReturnStmt(ReturnStmt *RS) {
    if (const Expr *V = RS->getRetValue())
      if (const VarDecl *D = dropLocalNamed(V))
        S.SafeCDiag(V->getExprLoc(), "drop-moved-by-name") << D << D->getName();
    if (const Expr *V = RS->getRetValue())
      if (const auto *DRE = dyn_cast<DeclRefExpr>(V->IgnoreParenImpCasts()))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
            VD && VD->isLocalVarDecl() && !VD->isStaticLocal())
          AllowedStructCopies.insert(V->IgnoreParens());
    return true;
  }

  void checkCallAliasing(CallExpr *Call) {
    const FunctionDecl *F = Call->getDirectCallee();
    if (!F)
      return;
    unsigned N = std::min(Call->getNumArgs(), F->getNumParams());
    // Rule 8: roots lent exclusively (or moved) must not appear elsewhere.
    SmallVector<std::pair<const VarDecl *, unsigned>, 4> Lent;
    for (unsigned I = 0; I != N; ++I) {
      QualType PT = F->getParamDecl(I)->getType();
      StringRef K = safeCPointerKind(PT);
      if (K != "excl" && !(K == "owned" || safeCTakesParam(F, I)))
        continue;
      if (const VarDecl *Root = lifetimes::safeCRootVar(Call->getArg(I)))
        Lent.push_back({Root, I});
      if (K == "excl")
        if (const ValueDecl *B = exclusiveArgBlocker(S.Context, Call->getArg(I), Body))
          S.SafeCDiag(Call->getArg(I)->getExprLoc(), "excl-arg-borrowed")
              << B;
      // Rule 7: an exclusive variable is never given away.
      if (K != "excl")
        if (const auto *DRE = dyn_cast<DeclRefExpr>(
                Call->getArg(I)->IgnoreParenImpCasts()))
          if (const auto *VD = dyn_cast<ValueDecl>(DRE->getDecl());
              VD && safeCPointerKind(VD->getType()) == "excl")
            S.SafeCDiag(DRE->getExprLoc(), "excl-reassigned") << VD << 1;
    }
    for (auto [Root, LentAt] : Lent)
      for (unsigned J = 0, NA = Call->getNumArgs(); J != NA; ++J)
        if (J != LentAt && Call->getArg(J)->getType()->isPointerType() &&
            lifetimes::safeCRootVar(Call->getArg(J)) == Root)
          S.SafeCDiag(Call->getArg(J)->getExprLoc(), "excl-aliased-arg")
              << Root;
  }

  bool VisitCallExpr(CallExpr *Call) {
    checkCallAliasing(Call);
    if (Call->getBuiltinCallee() == Builtin::BI__builtin_safec_take &&
        Call->getNumArgs() == 1)
      checkOwnershipRoot(S, Call->getArg(0), 0, Body);
    const Decl *Callee = Call->getCalleeDecl();
    if (!Callee)
      return true;
    for (unsigned I = 0, N = Call->getNumArgs(); I != N; ++I) {
      if (!safeCTakesParam(Callee, I))
        continue;
      if (const VarDecl *D = dropLocalNamed(Call->getArg(I))) {
        S.SafeCDiag(Call->getArg(I)->getExprLoc(), "drop-moved-by-name") << D << D->getName();
        continue;
      }
      // Only a named local or parameter is tracked by the lifetime analysis;
      // anything else (a field, element, global, or *p) still holds the
      // pointer after the free.
      const Expr *A = lifetimes::safeCPeel(Call->getArg(I));
      if (const auto *Take = dyn_cast<CallExpr>(A);
          Take && Take->getBuiltinCallee() == Builtin::BI__builtin_safec_take &&
          Take->getNumArgs() == 1 && !isOwnedSource(S.Context, Take)) {
        S.SafeCDiag(A->getExprLoc(), "free-borrowed-take")
            << describe(Take->getArg(0));
        continue;
      }
      if (!isTrackedLocal(A) && !isa<CallExpr>(A) &&
          !A->isNullPointerConstant(S.Context,
                                    Expr::NPC_ValueDependentIsNotNull))
        S.SafeCDiag(A->getExprLoc(), "free-in-place") << describe(A);
    }
    return true;
  }

  bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
    if (ICE->getCastKind() != CK_LValueToRValue)
      return true;
    // A compound literal is a new object, not a copy of one.
    if (ICE->getType()->isRecordType() && !AllowedStructCopies.count(ICE) &&
        !isa<CompoundLiteralExpr>(ICE->getSubExpr()->IgnoreParens()))
      if (const FieldDecl *Owned = ownedFieldOf(ICE->getType())) {
        S.SafeCDiag(ICE->getExprLoc(), "owning-struct-copy")
            << ICE->getType() << Owned;
        return true;
      }
    const ValueDecl *D = namedStorage(ICE->getSubExpr());
    if (D && hasKind(D, "unchecked") &&
        !AllowedUncheckedReads.count(ICE))
      S.SafeCDiag(ICE->getExprLoc(), "unchecked-read") << D;
    return true;
  }
};
} // namespace

void clang::checkSafeCPointerKindsIn(Sema &S, Stmt *Body) {
  if (Body && safeCOwnershipEnabled(S))
    PointerKindChecker(S, Body).TraverseStmt(Body);
}

void clang::checkSafeCPointerKindsIn(Sema &S, Decl *GlobalVar) {
  if (GlobalVar && safeCOwnershipEnabled(S))
    PointerKindChecker(S).TraverseDecl(GlobalVar);
}

/// Safe C: whether \p FD takes ownership of (frees) parameter \p Idx
/// (0-based), as the C library's free and realloc do.
bool clang::safeCTakesParam(const Decl *D, unsigned Idx) {
  if (const auto *FD = dyn_cast<FunctionDecl>(D))
    if (FD->isExternC() && FD->getIdentifier() && Idx == 0 &&
        llvm::is_contained({"free", "realloc", "reallocf", "fclose", "pclose",
                            "closedir"},
                           FD->getName()))
      return true;
  for (const auto *OA : D->specific_attrs<OwnershipAttr>())
    if (OA->isTakes())
      for (const ParamIdx &P : OA->args())
        if (P.getASTIndex() == Idx)
          return true;
  return false;
}


bool clang::checkSafeCTakeBuiltin(Sema &S, CallExpr *TheCall) {
  if (S.checkArgCount(TheCall, 1))
    return true;
  Expr *Arg = TheCall->getArg(0);
  QualType T = Arg->getType();
  if (!T->isPointerType() ||
      Arg->isModifiableLvalue(S.Context) != Expr::MLV_Valid) {
    S.SafeCDiag(Arg->getExprLoc(), "take-operand");
    return true;
  }
  // Taking leaves null behind, so the place must allow it.
  if (T->getNullability() != NullabilityKind::Nullable &&
      S.getLangOpts().NullabilitySafety) {
    const ValueDecl *D = namedStorage(Arg);
    S.SafeCDiag(Arg->getExprLoc(), "take-nonnull") << (D ? D->getName() : "");
    return true;
  }
  // The result is a plain pointer: a count field belongs to the struct, so
  // it cannot travel with the value.
  if (const auto *CAT = T->getAs<CountAttributedType>())
    T = CAT->desugar();
  TheCall->setType(T.getUnqualifiedType());
  return false;
}

namespace {
/// A tag place, as a root variable and the fields leading to the tag.
struct TagPath {
  const ValueDecl *Root = nullptr;
  llvm::SmallVector<const FieldDecl *, 3> Fields;
  bool operator==(const TagPath &O) const {
    return Root == O.Root && Fields == O.Fields;
  }
};

/// The path of a struct expression (`s`, `*p`, `p->a`, `v.b.c`), or none.
static std::optional<TagPath> structPath(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return TagPath{DRE->getDecl(), {}};
  if (const auto *UO = dyn_cast<UnaryOperator>(E);
      UO && UO->getOpcode() == UO_Deref)
    return structPath(UO->getSubExpr());
  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    std::optional<TagPath> P = structPath(ME->getBase());
    const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl());
    if (!P || !F)
      return std::nullopt;
    P->Fields.push_back(F);
    return P;
  }
  return std::nullopt;
}

/// The values a tag may hold on every path here (a case list can give
/// several, as `case RECT: case SQUARE:` does).
using TagValues = llvm::SmallVector<llvm::APSInt, 2>;
using TagFacts = llvm::SmallVector<std::pair<TagPath, TagValues>, 4>;

static const TagValues *lookupFact(const TagFacts &F, const TagPath &P) {
  for (const auto &[K, V] : F)
    if (K == P)
      return &V;
  return nullptr;
}
static void setFact(TagFacts &F, const TagPath &P,
                    std::optional<llvm::APSInt> V) {
  llvm::erase_if(F, [&](const auto &KV) { return KV.first == P; });
  if (V)
    F.push_back({P, TagValues{*V}});
}
static bool hasValue(const TagValues &Vs, const llvm::APSInt &V) {
  return llvm::any_of(Vs, [&](const llvm::APSInt &W) {
    return llvm::APSInt::isSameValue(W, V);
  });
}
static void killRoot(TagFacts &F, const ValueDecl *Root) {
  llvm::erase_if(F, [&](const auto &KV) { return KV.first.Root == Root; });
}

/// A tag field: an integer field whose struct has a union tagged by it.
static bool isTagField(const FieldDecl *F) {
  for (const FieldDecl *Sib : F->getParent()->fields())
    if (taggedByName(Sib->getType()) == F->getName())
      return true;
  return false;
}

/// For `x.tag == C` (either order, or !=): the tag path, constant, and
/// whether the comparison is ==.
static bool matchTagTest(const ASTContext &Ctx, const Expr *Cond, TagPath &P,
                         llvm::APSInt &V, bool &Eq) {
  Cond = Cond->IgnoreParenImpCasts();
  if (const auto *UO = dyn_cast<UnaryOperator>(Cond);
      UO && UO->getOpcode() == UO_LNot) {
    if (!matchTagTest(Ctx, UO->getSubExpr(), P, V, Eq))
      return false;
    Eq = !Eq;
    return true;
  }
  // A bare tag as a condition (`if (r.ok)`) tests it against true.
  if (const auto *ME = dyn_cast<MemberExpr>(Cond))
    if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl());
        F && isTagField(F) && F->getType()->isBooleanType())
      if (std::optional<TagPath> Path = structPath(ME)) {
        P = *Path;
        V = llvm::APSInt(llvm::APInt(Ctx.getIntWidth(F->getType()), 1),
                         /*isUnsigned=*/true);
        Eq = true;
        return true;
      }
  const auto *BO = dyn_cast<BinaryOperator>(Cond);
  if (!BO || !BO->isEqualityOp())
    return false;
  for (auto [A, B] : {std::pair(BO->getLHS(), BO->getRHS()),
                      std::pair(BO->getRHS(), BO->getLHS())}) {
    const auto *ME = dyn_cast<MemberExpr>(A->IgnoreParenImpCasts());
    const auto *F = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
    std::optional<llvm::APSInt> C = B->getIntegerConstantExpr(Ctx);
    if (!F || !C || !isTagField(F))
      continue;
    std::optional<TagPath> Path = structPath(A);
    if (!Path)
      continue;
    P = *Path;
    V = *C;
    Eq = BO->getOpcode() == BO_EQ;
    return true;
  }
  return false;
}

/// Updates facts for one statement (assignments to tags, kills).
static void transfer(const ASTContext &Ctx, const Stmt *St, TagFacts &F) {
  // `struct S s = {TAG, ...}`: an initializer sets each tag it names.
  if (const auto *DS = dyn_cast<DeclStmt>(St)) {
    for (const Decl *D : DS->decls())
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        killRoot(F, VD);
        const auto *ILE =
            VD->getInit()
                ? dyn_cast<InitListExpr>(VD->getInit()->IgnoreParenImpCasts())
                : nullptr;
        const RecordDecl *RD = VD->getType()->getAsRecordDecl();
        if (!ILE || !RD)
          continue;
        if (const InitListExpr *Sem = ILE->getSemanticForm())
          ILE = Sem;
        unsigned I = 0;
        for (const FieldDecl *Fld : RD->fields()) {
          if (Fld->isUnnamedBitField())
            continue;
          if (I >= ILE->getNumInits())
            break;
          const Expr *Init = ILE->getInit(I++);
          if (Init && isTagField(Fld))
            setFact(F, TagPath{VD, {Fld}}, Init->getIntegerConstantExpr(Ctx));
        }
      }
    return;
  }
  if (const auto *BO = dyn_cast<BinaryOperator>(St); BO && BO->isAssignmentOp()) {
    const Expr *L = BO->getLHS()->IgnoreParenImpCasts();
    if (const auto *ME = dyn_cast<MemberExpr>(L))
      if (const auto *Fld = dyn_cast<FieldDecl>(ME->getMemberDecl());
          Fld && isTagField(Fld))
        if (std::optional<TagPath> P = structPath(L)) {
          setFact(F, *P,
                  BO->getOpcode() == BO_Assign
                      ? BO->getRHS()->getIntegerConstantExpr(Ctx)
                      : std::nullopt);
          return;
        }
    // Reassigning a variable, or a struct holding a tag, forgets its tags.
    if (std::optional<TagPath> P = structPath(L))
      if (P->Fields.empty() || L->getType()->isRecordType())
        killRoot(F, P->Root);
    return;
  }
  if (const auto *UO = dyn_cast<UnaryOperator>(St);
      UO && UO->isIncrementDecrementOp())
    if (std::optional<TagPath> P = structPath(UO->getSubExpr()))
      setFact(F, *P, std::nullopt);
  // A call given a pointer to a modifiable object may change its tag.
  if (const auto *Call = dyn_cast<CallExpr>(St))
    for (const Expr *A : Call->arguments()) {
      QualType T = A->getType();
      if (!T->isPointerType() || T->getPointeeType().isConstQualified())
        continue;
      if (const VarDecl *Root = lifetimes::safeCRootVar(A))
        killRoot(F, Root);
    }
}

/// Facts on the edge from block B to successor index I (true = 0, false = 1
/// for an if; case labels for a switch).
static TagFacts edgeFacts(const ASTContext &Ctx, const CFGBlock &B,
                          unsigned I, const CFGBlock *Succ, TagFacts F) {
  const Stmt *Term = B.getTerminatorStmt();
  if (const Expr *Cond = B.getLastCondition(); Cond && !isa<SwitchStmt>(Term)) {
    TagPath P;
    llvm::APSInt V;
    bool Eq;
    if (matchTagTest(Ctx, Cond, P, V, Eq)) {
      if ((I == 0) == Eq)
        setFact(F, P, V);
      else if (P.Fields.back()->getType()->isBooleanType())
        // A bool that is not one value is the other.
        setFact(F, P, llvm::APSInt(llvm::APInt(V.getBitWidth(), V == 0),
                                   V.isUnsigned()));
    }
  }
  if (const auto *SS = dyn_cast_or_null<SwitchStmt>(Term); SS && Succ)
    if (const auto *CS = dyn_cast_or_null<CaseStmt>(Succ->getLabel()))
      if (const auto *ME =
              dyn_cast<MemberExpr>(SS->getCond()->IgnoreParenImpCasts()))
        if (const auto *Fld = dyn_cast<FieldDecl>(ME->getMemberDecl());
            Fld && isTagField(Fld) && !CS->getRHS())
          if (std::optional<TagPath> P = structPath(ME))
            if (std::optional<llvm::APSInt> V =
                    CS->getLHS()->getIntegerConstantExpr(Ctx))
              setFact(F, *P, *V);
  return F;
}

/// At a join: a tag stays known if both paths know it; it may then hold the
/// values either path allows.
static TagFacts meet(const TagFacts &A, const TagFacts &B) {
  TagFacts R;
  for (const auto &[K, V] : A)
    if (const TagValues *W = lookupFact(B, K)) {
      TagValues U = V;
      for (const llvm::APSInt &X : *W)
        if (!hasValue(U, X))
          U.push_back(X);
      R.push_back({K, U});
    }
  return R;
}
static bool sameFacts(const TagFacts &A, const TagFacts &B) {
  if (A.size() != B.size())
    return false;
  for (const auto &[K, V] : A) {
    const TagValues *W = lookupFact(B, K);
    if (!W || W->size() != V.size())
      return false;
  }
  return true;
}
} // namespace

void clang::checkSafeCTaggedAccess(Sema &S, Decl *Fn, Stmt *Body) {
  if (!Fn || !Body || !safeCOwnershipEnabled(S))
    return;
  // Quick exit: no member of a tagged union is named in this body.
  struct Finder : RecursiveASTVisitor<Finder> {
    bool Any = false;
    bool VisitMemberExpr(MemberExpr *ME) {
      if (const auto *F = dyn_cast<FieldDecl>(ME->getMemberDecl()))
        if (const RecordDecl *P = F->getParent(); P && P->isUnion() &&
            getSafeCAnnotation(P, "safec_tagged_by"))
          Any = true;
      return !Any;
    }
  } Find;
  Find.TraverseStmt(Body);
  if (!Find.Any)
    return;

  CFG::BuildOptions Opts;
  Opts.setAllAlwaysAdd(); // every member access is its own element
  std::unique_ptr<CFG> G = CFG::buildCFG(Fn, Body, &S.Context, Opts);
  if (!G)
    return;
  ASTContext &Ctx = S.Context;
  // In[b]: facts at block entry; Done marks blocks with a computed In.
  llvm::DenseMap<const CFGBlock *, TagFacts> In;
  llvm::DenseSet<const CFGBlock *> Done;
  llvm::SmallVector<const CFGBlock *, 16> Work;
  Done.insert(&G->getEntry());
  Work.push_back(&G->getEntry());
  auto Run = [&](const CFGBlock &B, TagFacts F, bool Report) {
    for (const CFGElement &E : B) {
      std::optional<CFGStmt> CS = E.getAs<CFGStmt>();
      if (!CS)
        continue;
      const Stmt *St = CS->getStmt();
      if (Report)
        if (const auto *ME = dyn_cast<MemberExpr>(St)) {
          const auto *M = dyn_cast<FieldDecl>(ME->getMemberDecl());
          const RecordDecl *U = M ? M->getParent() : nullptr;
          StringRef Tag = taggedByName(U);
          // The union is reached through its field in the tagged struct.
          const auto *UnionME =
              Tag.empty() ? nullptr
                          : dyn_cast<MemberExpr>(ME->getBase()->IgnoreParenImpCasts());
          if (UnionME)
            if (std::optional<TagPath> P = structPath(UnionME->getBase())) {
              const FieldDecl *TagF = nullptr;
              const RecordDecl *Outer =
                  cast<FieldDecl>(UnionME->getMemberDecl())->getParent();
              for (const FieldDecl *Sib : Outer->fields())
                if (Sib->getName() == Tag)
                  TagF = Sib;
              if (TagF) {
                P->Fields.push_back(TagF);
                const TagValues *Known = lookupFact(F, *P);
                // Fine when every value the tag may hold names this member.
                TagValues Mine;
                std::string Want;
                whenValues(Ctx, U, [&](const FieldDecl *Mem, llvm::APSInt V,
                                       SourceLocation) {
                  if (Mem != M)
                    return;
                  Mine.push_back(V);
                  if (Want.empty()) {
                    Want = TagF->getType()->isBooleanType()
                               ? (V == 0 ? "false" : "true")
                               : toString(V, 10);
                    if (const auto *ET = TagF->getType()->getAs<EnumType>())
                      for (const EnumConstantDecl *EC :
                           ET->getOriginalDecl()->getDefinitionOrSelf()->enumerators())
                        if (llvm::APSInt::isSameValue(EC->getInitVal(), V))
                          Want = EC->getName().str();
                  }
                });
                bool Ok = Known && llvm::all_of(*Known, [&](const llvm::APSInt &V) {
                  return hasValue(Mine, V);
                });
                if (!Ok && !Want.empty()) {
                  bool IsWrite = false; // set by the enclosing assignment
                  S.SafeCDiag(ME->getMemberLoc(), "union-member-unchecked")
                      << (IsWrite ? 1 : 0) << M << Tag << Want;
                }
              }
            }
        }
      transfer(Ctx, St, F);
    }
    return F;
  };
  // Forward dataflow to a fixed point, then one reporting pass.
  unsigned Steps = 0;
  while (!Work.empty() && ++Steps < 10000) {
    const CFGBlock *B = Work.pop_back_val();
    TagFacts Out = Run(*B, In[B], /*Report=*/false);
    unsigned I = 0;
    for (const CFGBlock::AdjacentBlock &A : B->succs()) {
      const CFGBlock *Succ = A.getReachableBlock();
      unsigned Idx = I++;
      if (!Succ)
        continue;
      TagFacts EF = edgeFacts(Ctx, *B, Idx, Succ, Out);
      if (Done.insert(Succ).second) {
        In[Succ] = EF;
        Work.push_back(Succ);
        continue;
      }
      TagFacts M = meet(In[Succ], EF);
      if (!sameFacts(M, In[Succ])) {
        In[Succ] = M;
        Work.push_back(Succ);
      }
    }
  }
  for (const CFGBlock *B : *G)
    if (Done.count(B))
      Run(*B, In[B], /*Report=*/true);
}
