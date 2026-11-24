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
#include "clang/Analysis/Analyses/MemberAccessPath.h"
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
     "'_Exclusive' (an exclusive borrow, like Rust's '&mut')%select{, or "
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
     "%0 is a shared borrow, but this parameter is '_Exclusive': the callee may "
     "free or replace what it owns while the owner still uses it; pass the "
     "owner, or declare %0 '_Owned' (if it owns the object) or '_Exclusive'"},
    {"no-bounds-checks-needs-unsafe", false,
     "%0 is '_No_bounds_checks', which compiles it without run-time bounds "
     "checks; that is a promise only an audited function can make, so mark "
     "it '__unsafe' too (and say in a comment why every access is in bounds)"},
    {"outparam-local-type", true,
     "the callee stores a pointer without bounds into %0, but a local "
     "pointer carries them by default; declare it as '%1;'"},
    {"drop-store", false,
     "%0 is freed automatically when its scope ends, so it may only hold "
     "memory it owns; store an allocation, '_Take(...)' of an owner, or "
     "null, not a pointer borrowed from a local or a parameter"},
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
     "%select{'if (x.%2 == %3)' or 'case %3:'|'if (x.%2)'|'if (!x.%2)'}4)"
     "%select{|, or set it ('x.%2 = %3;') before writing}0"},
    {"tagged-member-address", false,
     "a pointer to %0 would outlive a change of the tag '%1', after which "
     "these bytes may be another member's (one that holds pointers); use the "
     "member by name, or copy its value out ('x = s.%2;')"},
    {"wide-copy-unchecked", false,
     "%0 can write past the end of its destination, which nothing checks; "
     "use the size-bounded form, as in 'wcslcat(dst, src, N)' with N the "
     "destination's length in wide characters"},
    {"global-unshared", false,
     "%0 is a mutable global, so every thread shares it and two threads can "
     "write it at once; make it '_Thread_local' (one copy per thread, the "
     "usual fix for single-threaded code), '_Atomic', guarded by a lock "
     "('_Guarded_by(m)'), or 'const' if it never changes; if it is written "
     "only before any thread starts, mark it '_Unsynchronized'"},
    {"global-unshared-write", true, "%0 is written here"},
    {"unsynchronized-write", false,
     "%0 is '_Unsynchronized': it is shared without a lock, so a write is "
     "safe only before any thread starts; put this write inside "
     "'__unsafe { }' to say it is one of those"},
    {"signal-handler-global", false,
     "%0 is a signal handler, so it can run in the middle of any code, "
     "including code that is using %1; a handler may only touch globals "
     "that are '_Atomic', 'volatile sig_atomic_t' or 'const' (set a flag "
     "here and do the work in the main loop)"},
    {"signal-handler-global-use", true, "%0 is used here"},
    {"spawn-borrow", false,
     "the new thread may outlive what this pointer borrows (the spawning "
     "function can return or free it first); give the thread memory it owns "
     "(a new allocation, moved in) or static data"},
    {"used-after-spawn", false,
     "this memory was handed to a new thread, which may be using it right "
     "now; using it here too is a data race; give the thread its own copy, "
     "or share it through a lock"},
    {"spawned-here", true, "handed to the thread here"},
    {"union-member-untracked", false,
     "%0 is reached through something whose tag cannot be followed here (an "
     "array element or a call result); take a pointer to the struct first "
     "('struct T *e = &a[i];') and check 'e''s tag before using 'e->...'"},
    {"union-member-unwritten", false,
     "the tag '%1' now names %0, but %0 has not been written since the tag "
     "changed, so the union still holds another member's bytes; write %0 "
     "first"},
    {"union-init-mismatch", false,
     "this initializer sets the tag to %0 but fills %1, which is not live "
     "for that tag; initialize the member the tag names"},
    {"param-escape", false,
     "a borrow of %0 is stored where %1 points, which the caller can still "
     "read after %0 is gone; store something the caller lends or owns "
     "(a field of %1, or another parameter's data), or return the value "
     "instead; if a call here received it along with %1 and does not keep it, "
     "mark that function's parameter '_Noescape'"},
    {"global-borrow-stored", false,
     "this borrows memory a global owns, and storing it where %0 points hides "
     "that from the caller, who could use it after the global frees it; take "
     "the object that owns it as a parameter instead of reading the global"},
    {"global-borrow-returned", false,
     "this returns a borrow of memory a global owns, and callers in other "
     "files cannot see that, so they could use it after the global frees it; "
     "take the object that owns it as a parameter, return a copy "
     "('_Returns_owned'), or make the function 'static'"},
    {"noescape-stored", false,
     "%0 is '_Noescape', so callers count on it not being kept, but it is "
     "stored where %1 points; drop '_Noescape' from %0, and callers will keep "
     "what it borrows alive as long as they use %1"},
    {"param-escape-ends", true, "it ends here"},
    {"compound-literal-store", false,
     "this compound literal's storage ends with the block it is written in, "
     "and the pointer assigned here may outlive it; declare it as a named "
     "variable in the block where the pointer is used (as in "
     "'int vals[] = {1, 2};'), or initialize a new pointer with it"},
    {"used-after-drop", false,
     "this memory was freed when the '_Drop' variable holding it went out "
     "of scope; keep the variable alive as long as the copy, or move the "
     "pointer out of it with '_Take' to keep the memory"},
    {"dropped-here", true,
     "allocated into a '_Drop' variable here, which frees it at the end of "
     "its scope"},
    {"field-borrow-ended", false,
     "this pointer is borrowed from '%0', and the borrow ends (the field is "
     "taken, replaced, or lent to an exclusive parameter) before its last "
     "use; read '%0' again after that point, or finish using the pointer "
     "first"},
    {"field-borrow-ended-here", true, "the borrow ends here"},
    {"reborrow-ended", false,
     "%0 returns a borrow of '%1', which ends (an owned field of it is "
     "taken or replaced, or it is lent exclusively or given away) before "
     "this pointer's last use; call %0 again after that point, or finish "
     "using the pointer first"},
    {"view-write-through-pointer", false,
     "writing a borrow into a '_View' through a pointer can outlive "
     "what it points to, because the view's own lifetime is unknown here; "
     "write to a local view by name ('v = ...' or a field of 'v'), or write "
     "through the local '_Owned' pointer that holds the views ('struct %0 "
     "*_Owned v = calloc(n, sizeof *v)'), whose borrows are tracked"},
    {"view-memcpy", false,
     "copying bytes into '_View' structs through a pointer hides which "
     "borrows they hold; assign each one through the local '_Owned' pointer "
     "that holds them ('v[i] = other')"},
    {"borrow-slot", false,
     "%select{local|field}0 %1 points to a place that holds a "
     "borrow, so a store through it changes what that place borrows without "
     "naming it; %select{write '&place' where it is used instead of keeping it|"
     "pass the place's address as its own parameter}0"},
    {"borrow-slot-value", false,
     "this gives a pointer to a place that holds a borrow, but which place "
     "cannot be told from here; name it directly ('&place', or a parameter) in "
     "each branch"},
    {"view-pointer-storage", false,
     "%select{global|field}0 %1 points to '_View' structs, whose borrows are "
     "tracked only while a local or parameter holds the pointer; keep it in a "
     "local and pass it as its own parameter"},
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

/// A pointer to a place that holds a borrow (`int **`, `struct view *`). What
/// is written through it changes what the place borrows, so every use must
/// name that place: it may be a parameter, or `&place` written where it is
/// used, never a value that could be one of several places.
static bool isBorrowSlotPtr(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  if (!T->isPointerType())
    return false;
  StringRef Kind = safeCPointerKind(T);
  if (Kind == "owned" || Kind == "static" || Kind == "unchecked")
    return false;
  QualType Slot = T->getPointeeType();
  while (const ArrayType *AT = Slot->getAsArrayTypeUnsafe())
    Slot = AT->getElementType();
  return isViewType(Slot) ||
         (holdsBorrow(Slot) && safeCPointerKind(Slot).empty());
}

/// A pointer (at any depth) to `_View` structs.
static bool pointsToView(QualType T) {
  bool Ptr = false;
  for (;;) {
    if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
      T = AT->getElementType();
    else if (T->isPointerType()) {
      T = T->getPointeeType();
      Ptr = true;
    } else
      break;
  }
  return Ptr && isViewType(T);
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
    if (hasSafeCAnnotation(RD, "safec_view")) {
      // The inner views' borrows would be tracked by neither struct.
      for (const FieldDecl *FD : RD->fields())
        if (pointsToView(FD->getType()))
          S.SafeCDiag(FD->getLocation(), "view-pointer-storage") << 1 << FD;
        else if (isBorrowSlotPtr(FD->getType()))
          S.SafeCDiag(FD->getLocation(), "borrow-slot") << 1 << FD;
      return;
    }
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
      else if (pointsToView(FD->getType()))
        S.SafeCDiag(FD->getLocation(), "view-pointer-storage") << 1 << FD;
    }
    return;
  }
  if (auto *VD = dyn_cast<VarDecl>(D);
      VD && VD->hasGlobalStorage() && !isa<ParmVarDecl>(VD)) {
    if (holdsBorrow(VD->getType()) && !Kinded(VD))
      S.SafeCDiag(VD->getLocation(), "storage-borrow") << 1 << VD;
    else if (isViewType(VD->getType()))
      S.SafeCDiag(VD->getLocation(), "view-storage") << 0 << VD;
    else if (pointsToView(VD->getType()))
      S.SafeCDiag(VD->getLocation(), "view-pointer-storage") << 0 << VD;
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
  StringRef K = lifetimes::safeCDeclKind(Root);
  if (K == "owned" || K == "excl" || isOwnedStorage(Root) ||
      isInferredOwnerLocal(S.Context, Root, Body))
    return;
  S.SafeCDiag(Place->getExprLoc(), "ownership-through-borrow")
      << Op << Root->getName() << (isa<ParmVarDecl>(Root) ? 1 : 0);
}

static const ValueDecl *exclusiveArgBlocker(ASTContext &Ctx, const Expr *E,
                                            const Stmt *Body);
static bool isInferredExclusiveLocal(ASTContext &Ctx, const VarDecl *VD,
                                     const Stmt *Body);

/// A call that returns an exclusive reborrow: some `_Exclusive _Lifetimebound`
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
    if (lifetimes::safeCParamDeclKind(P) == "excl" &&
        P->hasAttr<LifetimeBoundAttr>() &&
        !exclusiveArgBlocker(Ctx, Call->getArg(I), Body))
      return true;
  }
  return false;
}

/// Whether Body assigns to VD, steps it, or takes its address.
static bool mayBeReassigned(const VarDecl *VD, const Stmt *Body) {
  if (!Body)
    return true;
  struct Finder : RecursiveASTVisitor<Finder> {
    const VarDecl *VD;
    bool Found = false;
    bool names(const Expr *E) {
      const auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
      return DRE && DRE->getDecl() == VD;
    }
    bool VisitBinaryOperator(BinaryOperator *BO) {
      Found |= BO->isAssignmentOp() && names(BO->getLHS());
      return !Found;
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      Found |= (UO->isIncrementDecrementOp() ||
                UO->getOpcode() == UO_AddrOf) &&
               names(UO->getSubExpr());
      return !Found;
    }
  } F;
  F.VD = VD;
  F.TraverseStmt(const_cast<Stmt *>(Body));
  return F.Found;
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
    // `&local` lends the variable itself, a stack object, whatever it holds
    // (strtod's end pointer, an out-parameter).
    if (UO->getOpcode() == UO_AddrOf &&
        isa<DeclRefExpr>(lifetimes::safeCPeel(UO->getSubExpr())))
      return nullptr;
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
  StringRef K = lifetimes::safeCDeclKind(VD);
  if (K == "owned" || K == "excl" || isOwnedStorage(VD) ||
      isInferredOwnerLocal(Ctx, VD, Body) ||
      isInferredExclusiveLocal(Ctx, VD, Body))
    return nullptr;
  return VD;
}

/// A plain local is an exclusive borrow, as a Rust `&mut` reborrow is, when
/// its address is never taken and every value stored in it is itself fine to
/// lend exclusively (from an exclusive parameter, an owner, an owned field of
/// one, or a stack object). `c = c->next` stays within c's own values.
static bool isInferredExclusiveLocal(ASTContext &Ctx, const VarDecl *VD,
                                     const Stmt *Body) {
  static thread_local llvm::SmallPtrSet<const VarDecl *, 8> Visiting;
  if (!VD->isLocalVarDecl() || VD->isStaticLocal() || !Body ||
      !VD->getType()->isPointerType() ||
      !safeCPointerKind(VD->getType()).empty())
    return false;
  if (!Visiting.insert(VD).second)
    return true;
  auto Fine = [&](const Expr *E) {
    return E->IgnoreParenCasts()->isNullPointerConstant(
               Ctx, Expr::NPC_ValueDependentIsNotNull) ||
           !exclusiveArgBlocker(Ctx, E, Body);
  };
  bool Ok = true;
  if (const Expr *Init = VD->getInit(); Init && !Fine(Init))
    Ok = false;
  struct Finder : RecursiveASTVisitor<Finder> {
    const VarDecl *VD;
    std::function<bool(const Expr *)> Fine;
    bool Ok = true;
    bool names(const Expr *E) {
      const auto *DRE = dyn_cast<DeclRefExpr>(E->IgnoreParenImpCasts());
      return DRE && DRE->getDecl() == VD;
    }
    bool VisitBinaryOperator(BinaryOperator *BO) {
      // `p += n` moves within the same object: still the same borrow.
      if (BO->isAssignmentOp() && names(BO->getLHS()) &&
          BO->getOpcode() != BO_AddAssign && BO->getOpcode() != BO_SubAssign &&
          (BO->getOpcode() != BO_Assign || !Fine(BO->getRHS())))
        Ok = false;
      return Ok;
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      if (UO->getOpcode() == UO_AddrOf && names(UO->getSubExpr()))
        Ok = false;
      return Ok;
    }
  };
  if (Ok) {
    Finder F{{}, VD, Fine};
    F.TraverseStmt(const_cast<Stmt *>(Body));
    Ok = F.Ok;
  }
  Visiting.erase(VD);
  return Ok;
}

/// A local whose cleanup frees it (`_Drop_free`, safec.h's trusted
/// cleanup).
static bool isDropFree(const VarDecl *VD) {
  const auto *CA = VD->getAttr<CleanupAttr>();
  const FunctionDecl *F = CA ? CA->getFunctionDecl() : nullptr;
  return F && llvm::any_of(F->specific_attrs<AnnotateAttr>(),
                           [](const AnnotateAttr *A) {
                             return A->getAnnotation() == "safec_cleanup";
                           });
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

/// A member of a tagged union that holds pointers in some member, named by
/// \p E (a member access), or null. Its address must not be taken: a later
/// tag change would let the pointer read another member's bytes.
static const FieldDecl *pointerUnionMember(const ASTContext &Ctx,
                                           const Expr *E, StringRef &Tag) {
  const auto *ME = dyn_cast<MemberExpr>(E->IgnoreParens());
  const auto *F = ME ? dyn_cast<FieldDecl>(ME->getMemberDecl()) : nullptr;
  const RecordDecl *U = F ? F->getParent() : nullptr;
  if (!U || !U->isUnion())
    return nullptr;
  Tag = taggedByName(U);
  if (Tag.empty())
    return nullptr;
  for (const FieldDecl *M : U->fields())
    if (!Sema::isSafeCPlainData(const_cast<ASTContext &>(Ctx), M->getType(),
                                /*VoidIsPlain=*/true))
      return F;
  return nullptr;
}

namespace {
class PointerKindChecker : public RecursiveASTVisitor<PointerKindChecker> {
  Sema &S;
  // Reads of an unchecked that are allowed: copies into another unchecked
  // and null tests. Filled by parents before their children are visited.
  llvm::SmallPtrSet<const Expr *, 8> AllowedUncheckedReads;

  void allowRead(const Expr *E) { AllowedUncheckedReads.insert(E->IgnoreParens()); }

  /// A borrow written into a view reached through a pointer (`v->p = x`,
  /// `v[i].p = x`, `(*v).p = x`) is tracked only when the pointer is a local
  /// that owns the views: the analysis adds x's loans to that local. Any
  /// other pointer may lead to views that outlive x.
  void checkViewWriteThroughPointer(const BinaryOperator *BO) {
    const Expr *LHS = BO->getLHS()->IgnoreParens();
    if (!holdsBorrow(LHS->getType()) && !isViewType(LHS->getType()))
      return;
    // Out to the view the write lands in: `(*v).in.p`, `v->arr[i]`.
    const Expr *Part = LHS;
    for (;;) {
      const auto *ME = dyn_cast<MemberExpr>(Part);
      const auto *ASE = dyn_cast<ArraySubscriptExpr>(Part);
      if (ME && !ME->isArrow() && isViewType(ME->getBase()->getType()))
        Part = ME->getBase()->IgnoreParens();
      else if (ASE &&
               ASE->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
        Part = ASE->getBase()->IgnoreParenImpCasts();
      else
        break;
    }
    const Expr *Ptr = nullptr;
    QualType View;
    if (const auto *ME = dyn_cast<MemberExpr>(Part);
        ME && ME->isArrow() &&
        isViewType(ME->getBase()->getType()->getPointeeType())) {
      Ptr = ME->getBase();
      View = ME->getBase()->getType()->getPointeeType();
    } else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(Part);
               ASE && isViewType(ASE->getType()) &&
               ASE->getBase()->IgnoreParenImpCasts()->getType()->isPointerType()) {
      Ptr = ASE->getBase();
      View = ASE->getType();
    } else if (const auto *UO = dyn_cast<UnaryOperator>(Part);
               UO && UO->getOpcode() == UO_Deref &&
               isViewType(UO->getType())) {
      Ptr = UO->getSubExpr();
      View = UO->getType();
    }
    if (!Ptr)
      return;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Ptr->IgnoreParenImpCasts()))
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
          VD && VD->isLocalVarDecl() && !VD->isStaticLocal() &&
          safeCPointerKind(VD->getType()) == "owned")
        return;
    // `*out = v` through a parameter: the caller takes what out points to as
    // borrowing every other argument (safeCFlowThroughOutArgs), and a borrow
    // of a local is an escape. A parameter that may have been pointed
    // elsewhere could lead to other views.
    if (isViewType(LHS->getType()))
      if (const auto *DRE = dyn_cast<DeclRefExpr>(Ptr->IgnoreParenImpCasts()))
        if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl());
            PVD && !mayBeReassigned(PVD, Body))
          return;
    const RecordDecl *RD = View->getAsRecordDecl();
    S.SafeCDiag(BO->getOperatorLoc(), "view-write-through-pointer")
        << (RD ? RD->getName() : StringRef("view"));
  }

  void checkStore(const ValueDecl *Dst, const Expr *Src, SourceLocation Loc) {
    if (!Dst)
      return;
    // A _Drop_free local is freed when its scope ends, so it may only hold
    // memory it owns (found by the Lean model of _Drop: `p = &local` or a
    // borrowed parameter made the cleanup free memory it did not own).
    if (const auto *VD = dyn_cast<VarDecl>(Dst);
        VD && VD->getType()->isPointerType() && isDropFree(VD) &&
        !isOwnedSource(S.Context, Src))
      S.SafeCDiag(Src->getExprLoc(), "drop-store") << VD;
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
    if (BO->getOpcode() == BO_Comma ||
        (BO->getOpcode() == BO_Assign && isBorrowSlotPtr(BO->getLHS()->getType())))
      checkSlotValue(BO);
    checkSigactionStore(BO);
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
      checkViewWriteThroughPointer(BO);
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

  bool VisitAbstractConditionalOperator(AbstractConditionalOperator *E) {
    return checkSlotValue(E);
  }
  bool VisitStmtExpr(StmtExpr *E) { return checkSlotValue(E); }
  /// A call's result used as such a pointer (an ignored one is harmless).
  void checkSlotCall(const Expr *E) {
    const auto *Call =
        dyn_cast<CallExpr>(lifetimes::safeCPeel(E)->IgnoreParenCasts());
    if (Call && Call->getBuiltinCallee() != Builtin::BI__builtin_safec_take)
      checkSlotValue(Call);
  }
  bool VisitMemberExpr(MemberExpr *E) {
    if (E->isArrow())
      checkSlotCall(E->getBase());
    return true;
  }
  bool VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    checkSlotCall(E->getBase());
    return true;
  }
  bool checkSlotValue(const Expr *E) {
    QualType T = E->getType();
    if (isBorrowSlotPtr(T) ||
        (T->isPointerType() && isBorrowSlotPtr(T->getPointeeType())))
      S.SafeCDiag(E->getExprLoc(), "borrow-slot-value");
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (VD->isLocalVarDecl() && isBorrowSlotPtr(VD->getType()))
      S.SafeCDiag(VD->getLocation(), "borrow-slot") << 0 << VD;
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
      auto Require = [&](llvm::APSInt V, std::string Name) {
        for (const llvm::APSInt &C : Cases)
          if (llvm::APSInt::isSameValue(C, V))
            return;
        S.SafeCDiag(SS->getSwitchLoc(), "switch-not-exhaustive")
            << TagField->getName() << Name;
      };
      // An enum tag's every enumerator is a variant, with or without a member
      // holding data (Rust's unit variants); an integer tag's variants are
      // the values its members name.
      if (const auto *ET = TagField->getType()->getAs<EnumType>()) {
        for (const EnumConstantDecl *E :
             ET->getOriginalDecl()->getDefinitionOrSelf()->enumerators())
          Require(E->getInitVal(), E->getName().str());
      } else {
        whenValues(S.Context, F->getType()->getAsRecordDecl(),
                   [&](const FieldDecl *, llvm::APSInt V, SourceLocation) {
                     Require(V, toString(V, 10));
                   });
      }
      break;
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *UO) {
    if (UO->getOpcode() == UO_Deref)
      checkSlotCall(UO->getSubExpr());
    // One level deeper: `&out`, where out is such a pointer itself.
    if (UO->getOpcode() == UO_AddrOf &&
        isBorrowSlotPtr(UO->getSubExpr()->getType()))
      checkSlotValue(UO);
    StringRef Tag;
    if (UO->getOpcode() == UO_AddrOf)
      if (const FieldDecl *M = pointerUnionMember(S.Context, UO->getSubExpr(), Tag))
        S.SafeCDiag(UO->getOperatorLoc(), "tagged-member-address")
            << M << Tag << M->getName();
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

  /// Whether data reached through \p T can be freed: it holds an owner,
  /// directly or in a member. Other pointer members are _Static or
  /// _Unchecked (fields hold no borrows), so replacing them cannot dangle.
  static bool reachesOwner(QualType T, unsigned Depth = 0) {
    if (Depth > 8)
      return true;
    // Pointer kinds are type attributes: read them before any desugaring.
    if (T->isPointerType())
      return safeCPointerKind(T) == "owned";
    if (const ArrayType *AT = T->getAsArrayTypeUnsafe())
      return reachesOwner(AT->getElementType(), Depth + 1);
    if (const RecordDecl *RD = T->getAsRecordDecl()) {
      const RecordDecl *Def = RD->getDefinition();
      if (!Def)
        return true; // opaque: assume it can
      for (const FieldDecl *FD : Def->fields())
        if (reachesOwner(FD->getType(), Depth + 1))
          return true;
    }
    return false;
  }
  /// A call lending argument I may free or replace what other arguments
  /// reach: it is given away, or it is lent exclusively and what it points
  /// to holds an owner.
  static bool lentCanFree(const FunctionDecl *F, unsigned I) {
    QualType PT = F->getParamDecl(I)->getType();
    if (lifetimes::safeCParamDeclKind(F->getParamDecl(I)) != "excl")
      return true;
    return reachesOwner(PT->getPointeeType());
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
      StringRef K = lifetimes::safeCParamDeclKind(F->getParamDecl(I));
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
              VD && lifetimes::safeCDeclKind(VD) == "excl")
            S.SafeCDiag(DRE->getExprLoc(), "excl-reassigned") << VD << 1;
    }
    for (auto [Root, LentAt] : Lent)
      for (unsigned J = 0, NA = Call->getNumArgs(); J != NA; ++J)
        if (J != LentAt && Call->getArg(J)->getType()->isPointerType() &&
            lifetimes::safeCRootVar(Call->getArg(J)) == Root &&
            lentCanFree(F, LentAt))
          S.SafeCDiag(Call->getArg(J)->getExprLoc(), "excl-aliased-arg")
              << Root;
  }

  /// M5: a global a signal handler may not touch (C11 7.14.1.1p5 allows
  /// only lock-free atomics and volatile sig_atomic_t), searched through
  /// the handler's calls to functions with bodies. Sets \p Use.
  static const VarDecl *signalUnsafeGlobal(const FunctionDecl *FD,
                                           const DeclRefExpr *&Use,
                                           unsigned Depth) {
    const FunctionDecl *Def = nullptr;
    if (Depth > 6 || !FD->hasBody(Def) || !Def)
      return nullptr;
    struct Finder : RecursiveASTVisitor<Finder> {
      unsigned Depth;
      const VarDecl *Found = nullptr;
      const DeclRefExpr *Use = nullptr;
      bool VisitDeclRefExpr(DeclRefExpr *DRE) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
            VD && VD->hasGlobalStorage() && !VD->isStaticLocal()) {
          QualType T = VD->getType();
          bool OK = T.isConstQualified() || T->isAtomicType() ||
                    (T.isVolatileQualified() && T->isIntegerType()) ||
                    VD->getTLSKind() != VarDecl::TLS_None;
          if (!OK) {
            Found = VD;
            Use = DRE;
            return false;
          }
        }
        if (const auto *Callee = dyn_cast<FunctionDecl>(DRE->getDecl())) {
          const DeclRefExpr *Inner = nullptr;
          if (const VarDecl *G = signalUnsafeGlobal(Callee, Inner, Depth + 1)) {
            Found = G;
            Use = Inner;
            return false;
          }
        }
        return true;
      }
    } F;
    F.Depth = Depth;
    F.TraverseStmt(Def->getBody());
    Use = F.Use;
    return F.Found;
  }

  void checkSignalHandler(const Expr *Handler) {
    const auto *DRE = dyn_cast<DeclRefExpr>(Handler->IgnoreParenCasts());
    const auto *FD = DRE ? dyn_cast<FunctionDecl>(DRE->getDecl()) : nullptr;
    if (!FD)
      return;
    const DeclRefExpr *Use = nullptr;
    if (const VarDecl *G = signalUnsafeGlobal(FD, Use, 0)) {
      S.SafeCDiag(Handler->getExprLoc(), "signal-handler-global") << FD << G;
      if (Use)
        S.SafeCDiag(Use->getExprLoc(), "signal-handler-global-use") << G;
    }
  }

  void checkSigactionStore(BinaryOperator *BO) {
    // sigaction's handler fields.
    if (BO->getOpcode() == BO_Assign)
      if (const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens()))
        if (const auto *FDecl = dyn_cast<FieldDecl>(ME->getMemberDecl());
            FDecl && FDecl->getIdentifier() &&
            (FDecl->getName() == "sa_handler" ||
             FDecl->getName() == "sa_sigaction" ||
             FDecl->getName() == "__sa_handler"))
          checkSignalHandler(BO->getRHS());
  }

  bool VisitCallExpr(CallExpr *Call) {
    for (const Expr *Arg : Call->arguments())
      checkSlotCall(Arg);
    checkCallAliasing(Call);
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->isExternC() && F->getIdentifier() && Call->getNumArgs() >= 2) {
      StringRef N = F->getName();
      N.consume_front("__builtin_");
      N.consume_front("__");
      N.consume_back("_chk");
      if (N == "memcpy" || N == "memmove" || N == "mempcpy") {
        const Expr *Dst =
            lifetimes::safeCPeel(Call->getArg(0))->IgnoreParenCasts();
        const auto *AO = dyn_cast<UnaryOperator>(Dst);
        bool LocalView = AO && AO->getOpcode() == UO_AddrOf &&
                         isTrackedLocal(AO->getSubExpr());
        if (!LocalView && pointsToView(Dst->getType()))
          S.SafeCDiag(Dst->getExprLoc(), "view-memcpy");
      }
    }
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->isExternC() && F->getIdentifier() &&
        F->getName() == "signal" && Call->getNumArgs() == 2)
      checkSignalHandler(Call->getArg(1));
    // M5 rule 1: a thread's argument is moved into it (Rust's 'static +
    // Send bound on spawn): an owner, static data, or null.
    if (const FunctionDecl *F = Call->getDirectCallee();
        F && F->isExternC() && F->getIdentifier() &&
        F->getName() == "pthread_create" && Call->getNumArgs() == 4) {
      const Expr *Arg = Call->getArg(3);
      const auto *ArgVar = dyn_cast_or_null<VarDecl>(namedStorage(Arg));
      bool OwnerVar = ArgVar && (isOwnedStorage(ArgVar) ||
                                 isInferredOwnerLocal(S.Context, ArgVar, Body));
      if (!OwnerVar && !isOwnedSource(S.Context, Arg) &&
          !isStaticAddress(S.Context, Arg))
        S.SafeCDiag(Arg->getExprLoc(), "spawn-borrow");
    }
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
    if (ICE->getCastKind() == CK_ArrayToPointerDecay) {
      StringRef Tag;
      if (const FieldDecl *M = pointerUnionMember(S.Context, ICE->getSubExpr(), Tag))
        S.SafeCDiag(ICE->getExprLoc(), "tagged-member-address")
            << M << Tag << M->getName();
      return true;
    }
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
/// A tag place: the shared member path type (also used by the nullability
/// analysis), so tag and null facts are keyed the same way.
using TagPath = MemberAccessPath;

/// The path of a struct expression: a variable itself (no fields), or a
/// member chain (`p->a`, `(*p).a`, `v.b.c`).
static std::optional<TagPath> structPath(const Expr *E) {
  if (const auto *DRE =
          dyn_cast<DeclRefExpr>(ignoreBoundsSafetyWrappers(E)))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      return TagPath{VD, {}};
  if (const auto *UO =
          dyn_cast<UnaryOperator>(ignoreBoundsSafetyWrappers(E));
      UO && UO->getOpcode() == UO_Deref)
    return structPath(UO->getSubExpr());
  return decomposeMemberAccess(E);
}

/// The values a tag may hold on every path here (a case list can give
/// several, as `case RECT: case SQUARE:` does).
/// Written is false after a tag write until the member it names is written:
/// until then the union still holds the old member's bytes.
struct TagValues : llvm::SmallVector<llvm::APSInt, 2> {
  using llvm::SmallVector<llvm::APSInt, 2>::SmallVector;
  bool Written = true;
};
using TagFacts = llvm::SmallVector<std::pair<TagPath, TagValues>, 4>;

static const TagValues *lookupFact(const TagFacts &F, const TagPath &P) {
  for (const auto &[K, V] : F)
    if (K == P)
      return &V;
  return nullptr;
}
static void setFact(TagFacts &F, const TagPath &P,
                    std::optional<llvm::APSInt> V, bool Written = true) {
  llvm::erase_if(F, [&](const auto &KV) { return KV.first == P; });
  if (V || !Written) {
    // No value: the tag is unknown, but an unwritten member stays known.
    TagValues Vs;
    if (V)
      Vs.push_back(*V);
    Vs.Written = Written;
    F.push_back({P, Vs});
  }
}
// A tag changed somewhere this analysis cannot see the member written: the
// tag is unknown and the member must be written before it is read.
static void unwriteIf(TagFacts &F,
                      llvm::function_ref<bool(const TagPath &)> Pred) {
  for (auto &KV : F)
    if (Pred(KV.first)) {
      KV.second.clear();
      KV.second.Written = false;
    }
}
static bool hasValue(const TagValues &Vs, const llvm::APSInt &V) {
  return llvm::any_of(Vs, [&](const llvm::APSInt &W) {
    return llvm::APSInt::isSameValue(W, V);
  });
}
// Forgetting a tag keeps one thing: that its member was not written since
// the tag last changed. An empty value set is "tag unknown"; a later test
// narrows it again but must not make the member look written (the union
// still holds the old member's bytes).
static void forgetFact(TagFacts &F, const TagPath &P) {
  for (auto I = F.begin(); I != F.end(); ++I)
    if (I->first == P) {
      if (I->second.Written)
        F.erase(I);
      else
        I->second.clear();
      return;
    }
}
static void killRoot(TagFacts &F, const ValueDecl *Root) {
  llvm::SmallVector<TagPath, 2> Paths;
  for (const auto &KV : F)
    if (KV.first.Root == Root)
      Paths.push_back(KV.first);
  for (const TagPath &P : Paths)
    forgetFact(F, P);
}
// A tag test narrows the values the tag may hold; whether the member was
// written since the tag changed stays as it was.
static void narrowFact(TagFacts &F, const TagPath &P, llvm::APSInt V) {
  const TagValues *Old = lookupFact(F, P);
  setFact(F, P, V, Old ? Old->Written : true);
}

/// A tag field: an integer field whose struct has a union tagged by it.
static bool isTagField(const FieldDecl *F) {
  // An unnamed field (an anonymous union) is never a tag; without this its
  // empty name would match the empty name of a non-union sibling.
  if (F->getName().empty())
    return false;
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

/// Whether a fact about \p Root can be changed through some other path: a
/// parameter or global, a pointer, or a local whose address was taken.
static bool mayBeAliased(const VarDecl *Root,
                         const llvm::DenseSet<const VarDecl *> &AddrTaken) {
  return !Root || isa<ParmVarDecl>(Root) || Root->hasGlobalStorage() ||
         Root->getType()->isPointerType() || AddrTaken.count(Root);
}

/// Updates facts for one statement (assignments to tags, kills).
static void transfer(const ASTContext &Ctx, const Stmt *St, TagFacts &F,
                     const llvm::DenseSet<const VarDecl *> &AddrTaken) {
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
          // The same tag of another struct may be this one under another
          // name (`q = &x; q->kind = ...`): forget those.
          unwriteIf(F, [&](const TagPath &Q) {
            return Q.Fields.back() == Fld && Q.Root != P->Root &&
                   mayBeAliased(Q.Root, AddrTaken);
          });
          // The union still holds the old member's bytes until the newly
          // named member is written.
          setFact(F, *P,
                  BO->getOpcode() == BO_Assign
                      ? BO->getRHS()->getIntegerConstantExpr(Ctx)
                      : std::nullopt,
                  /*Written=*/false);
          return;
        } else {
          // A tag written through a place the analysis cannot name (a[j].k):
          // it may be any struct's tag of this kind.
          unwriteIf(F, [&](const TagPath &Q) { return Q.Fields.back() == Fld; });
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
      forgetFact(F, *P);
  // A call may change any tag it can reach: through a modifiable pointer
  // argument, or through globals (any function not known to touch none).
  if (const auto *Call = dyn_cast<CallExpr>(St)) {
    const FunctionDecl *Callee = Call->getDirectCallee();
    bool CallsBack = llvm::any_of(Call->arguments(), [](const Expr *A) {
      return A->getType()->isFunctionPointerType();
    });
    bool Library = Callee && (Call->getBuiltinCallee() ||
                              Ctx.getSourceManager().isInSystemHeader(
                                  Callee->getCanonicalDecl()->getLocation()));
    bool Pure = Callee && (Callee->hasAttr<PureAttr>() ||
                           Callee->hasAttr<ConstAttr>());
    bool Modifies = !(Pure || (Library && !CallsBack));
    for (const Expr *A : Call->arguments()) {
      QualType T = A->getType();
      if (!T->isPointerType() || T->getPointeeType().isConstQualified())
        continue;
      Modifies = true;
      if (const VarDecl *Root = lifetimes::safeCRootVar(A))
        killRoot(F, Root);
    }
    if (Modifies) {
      llvm::SmallVector<TagPath, 2> Paths;
      for (const auto &KV : F)
        if (mayBeAliased(KV.first.Root, AddrTaken))
          Paths.push_back(KV.first);
      for (const TagPath &P : Paths)
        forgetFact(F, P);
    }
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
        narrowFact(F, P, V);
      else if (P.Fields.back()->getType()->isBooleanType())
        // A bool that is not one value is the other.
        narrowFact(F, P, llvm::APSInt(llvm::APInt(V.getBitWidth(), V == 0),
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
              narrowFact(F, *P, *V);
  return F;
}

/// At a join: a tag stays known if both paths know it; it may then hold the
/// values either path allows.
static TagFacts meet(const TagFacts &A, const TagFacts &B) {
  TagFacts R;
  for (const auto &[K, V] : A)
    if (const TagValues *W = lookupFact(B, K)) {
      TagValues U;
      // Unknown on either path (an empty set) stays unknown.
      if (!V.empty() && !W->empty()) {
        U = V;
        for (const llvm::APSInt &X : *W)
          if (!hasValue(U, X))
            U.push_back(X);
      }
      U.Written = V.Written && W->Written;
      if (!U.empty() || !U.Written)
        R.push_back({K, U});
    } else if (!V.Written) {
      // Known on one path only: the tag is unknown, but an unwritten member
      // on that path must still be written before it is read.
      TagValues U;
      U.Written = false;
      R.push_back({K, U});
    }
  for (const auto &[K, W] : B)
    if (!lookupFact(A, K) && !W.Written) {
      TagValues U;
      U.Written = false;
      R.push_back({K, U});
    }
  return R;
}
static bool sameFacts(const TagFacts &A, const TagFacts &B) {
  if (A.size() != B.size())
    return false;
  for (const auto &[K, V] : A) {
    const TagValues *W = lookupFact(B, K);
    if (!W || W->size() != V.size() || W->Written != V.Written)
      return false;
  }
  return true;
}
} // namespace

/// `struct T x = {.tag = V, .m = ...}` must fill a member live for V.
static void checkTagInits(Sema &S, const DeclStmt *DS) {
  for (const Decl *D : DS->decls()) {
    const auto *VD = dyn_cast<VarDecl>(D);
    const auto *ILE = VD && VD->getInit() ? dyn_cast<InitListExpr>(
                                                VD->getInit()->IgnoreParenImpCasts())
                                          : nullptr;
    const RecordDecl *RD = VD ? VD->getType()->getAsRecordDecl() : nullptr;
    if (!ILE || !RD)
      continue;
    if (const InitListExpr *Sem = ILE->getSemanticForm())
      ILE = Sem;
    std::optional<llvm::APSInt> TagV;
    const FieldDecl *TagF = nullptr;
    unsigned I = 0;
    llvm::SmallVector<std::pair<const FieldDecl *, const Expr *>, 4> Inits;
    for (const FieldDecl *F : RD->fields()) {
      if (F->isUnnamedBitField())
        continue;
      if (I >= ILE->getNumInits())
        break;
      Inits.push_back({F, ILE->getInit(I++)});
    }
    for (auto [F, Init] : Inits)
      if (Init && isTagField(F)) {
        TagF = F;
        TagV = Init->getIntegerConstantExpr(S.Context);
      }
    if (!TagF || !TagV)
      continue;
    for (auto [F, Init] : Inits) {
      if (taggedByName(F->getType()) != TagF->getName() || !Init)
        continue;
      const auto *UILE = dyn_cast<InitListExpr>(Init->IgnoreParenImpCasts());
      if (!UILE)
        continue;
      if (const InitListExpr *Sem = UILE->getSemanticForm())
        UILE = Sem;
      const FieldDecl *Member = UILE->getInitializedFieldInUnion();
      if (!Member || UILE->getNumInits() == 0)
        continue;
      bool Live = false;
      whenValues(S.Context, F->getType()->getAsRecordDecl(),
                 [&](const FieldDecl *M, llvm::APSInt V, SourceLocation) {
                   if (M == Member && llvm::APSInt::isSameValue(V, *TagV))
                     Live = true;
                 });
      if (!Live)
        S.SafeCDiag(UILE->getBeginLoc(), "union-init-mismatch")
            << toString(*TagV, 10) << Member;
    }
  }
}

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

  // Locals whose address escapes may change behind a name the analysis sees.
  struct AddrFinder : RecursiveASTVisitor<AddrFinder> {
    llvm::DenseSet<const VarDecl *> Taken;
    void note(const Expr *E) {
      while (true) {
        E = E->IgnoreParenImpCasts();
        if (const auto *ME = dyn_cast<MemberExpr>(E); ME && !ME->isArrow())
          E = ME->getBase();
        else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
          E = ASE->getBase();
        else
          break;
      }
      if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          Taken.insert(VD);
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      if (UO->getOpcode() == UO_AddrOf)
        note(UO->getSubExpr());
      return true;
    }
    bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
      if (ICE->getCastKind() == CK_ArrayToPointerDecay)
        note(ICE->getSubExpr());
      return true;
    }
  } Addr;
  Addr.TraverseStmt(Body);

  // Members written (`x.m = v`): a write needs the tag set, and makes the
  // member written; a read needs both.
  struct LHSFinder : RecursiveASTVisitor<LHSFinder> {
    llvm::DenseSet<const MemberExpr *> Writes;
    bool VisitBinaryOperator(BinaryOperator *BO) {
      if (BO->getOpcode() == BO_Assign)
        if (const auto *ME = dyn_cast<MemberExpr>(BO->getLHS()->IgnoreParens()))
          Writes.insert(ME);
      return true;
    }
  } LHS;
  LHS.TraverseStmt(Body);

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
        if (const auto *DS = dyn_cast<DeclStmt>(St))
          checkTagInits(S, DS);
      {
        if (const auto *ME = dyn_cast<MemberExpr>(St)) {
          const auto *M = dyn_cast<FieldDecl>(ME->getMemberDecl());
          const RecordDecl *U = M ? M->getParent() : nullptr;
          StringRef Tag = taggedByName(U);
          // The union is reached through its field in the tagged struct.
          const auto *UnionME =
              Tag.empty() ? nullptr
                          : dyn_cast<MemberExpr>(ME->getBase()->IgnoreParenImpCasts());
          bool IsWrite = LHS.Writes.count(ME);
          std::optional<TagPath> P =
              UnionME ? structPath(UnionME->getBase()) : std::nullopt;
          if (UnionME && !P && Report)
            // Reached through something the analysis cannot follow (an
            // array element, a call result): it cannot know the tag.
            S.SafeCDiag(ME->getMemberLoc(), "union-member-untracked") << M;
          if (UnionME && P) {
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
                bool Ok = Known && !Known->empty() &&
                          llvm::all_of(*Known, [&](const llvm::APSInt &V) {
                  return hasValue(Mine, V);
                });
                if (Ok && IsWrite)
                  for (auto &KV : F)
                    if (KV.first == *P)
                      KV.second.Written = true;
                bool Unwritten = Ok && !IsWrite && !Known->Written;
                if (Unwritten && Report)
                  S.SafeCDiag(ME->getMemberLoc(), "union-member-unwritten")
                      << M << Tag;
                if (!Ok && !Want.empty()) {
                  unsigned Hint = !TagF->getType()->isBooleanType() ? 0
                                  : Want == "true"                   ? 1
                                                                     : 2;
                  if (Report)
                    S.SafeCDiag(ME->getMemberLoc(), "union-member-unchecked")
                        << (IsWrite ? 1 : 0) << M << Tag << Want << Hint;
                }
              }
            }
        }
      }
      transfer(Ctx, St, F, Addr.Taken);
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

// M5 rule 2 (Rust's `static` must be `Sync`): a mutable global is shared by
// every thread, so it must be one copy per thread, atomic, or behind a lock.
// A file-static global that its file never writes (no assignment, ++/--,
// address taken, or array handed out as writable) is a constant in all but
// name, as Juliet's `static int staticTrue = 1;` is, and needs nothing.
static llvm::cl::opt<bool> SafeCSharedGlobals(
    "safe-c-shared-globals", llvm::cl::init(true),
    llvm::cl::desc("Safe C: require mutable globals to be thread-local, "
                   "atomic, guarded or unsynchronized (M5 rule 2)"));

void clang::checkSafeCSharedGlobals(Sema &S) {
  SourceManager &SM = S.getSourceManager();
  auto Shareable = [](const VarDecl *VD) {
    QualType T = VD->getType();
    QualType Base = VD->getASTContext().getBaseElementType(T);
    if (T.isConstQualified() || Base.isConstQualified() || T->isAtomicType() ||
        Base->isAtomicType() || VD->getTLSKind() != VarDecl::TLS_None ||
        VD->hasAttr<GuardedByAttr>() || VD->hasAttr<PtGuardedByAttr>())
      return true;
    // volatile sig_atomic_t: C's own type for flags set by signal handlers.
    // Locks and once-flags synchronize themselves.
    for (QualType Cur = Base; const auto *TT = Cur->getAs<TypedefType>();
         Cur = TT->getDecl()->getUnderlyingType()) {
      StringRef N = TT->getDecl()->getName();
      if ((N == "sig_atomic_t" && T.isVolatileQualified()) ||
          N == "pthread_mutex_t" || N == "pthread_rwlock_t" ||
          N == "pthread_cond_t" || N == "pthread_once_t" ||
          N == "pthread_spinlock_t" || N == "mtx_t" || N == "cnd_t" ||
          N == "once_flag")
        return true;
    }
    if (const RecordDecl *RD = Base->getAsRecordDecl())
      if (RD->hasAttr<CapabilityAttr>())
        return true;
    return false;
  };
  auto Unsynchronized = [](const VarDecl *VD) {
    for (const VarDecl *R : VD->redecls())
      for (const auto *A : R->specific_attrs<AnnotateAttr>())
        if (A->getAnnotation() == "safec_unsynchronized")
          return true;
    return false;
  };
  struct Finder : RecursiveASTVisitor<Finder> {
    llvm::DenseMap<const VarDecl *, const Expr *> Written;
    llvm::SmallVector<const VarDecl *, 16> Globals;
    // Decays and address-ofs only read through (a pointer-to-const, or a
    // subscript).
    llvm::SmallPtrSet<const Expr *, 8> ReadOnlyDecay;
    bool shouldVisitImplicitCode() const { return false; }
    static const VarDecl *root(const Expr *E) {
      while (true) {
        E = E->IgnoreParenImpCasts();
        if (const auto *ME = dyn_cast<MemberExpr>(E); ME && !ME->isArrow())
          E = ME->getBase();
        else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E);
                 ASE && ASE->getBase()->IgnoreParenImpCasts()->getType()->isArrayType())
          E = ASE->getBase();
        else
          break;
      }
      const auto *DRE = dyn_cast<DeclRefExpr>(E);
      const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
      return VD && VD->hasGlobalStorage() ? VD : nullptr;
    }
    llvm::SmallVector<std::pair<const VarDecl *, const Expr *>, 8> AllWrites;
    void write(const Expr *Place, const Expr *At) {
      if (const VarDecl *VD = root(Place)) {
        Written.try_emplace(VD->getCanonicalDecl(), At);
        AllWrites.push_back({VD->getCanonicalDecl(), At});
      }
    }
    bool VisitVarDecl(VarDecl *VD) {
      if (VD->hasGlobalStorage() && !isa<ParmVarDecl>(VD) &&
          VD->isThisDeclarationADefinition() != VarDecl::DeclarationOnly)
        Globals.push_back(VD);
      return true;
    }
    bool VisitBinaryOperator(BinaryOperator *BO) {
      if (BO->isAssignmentOp())
        write(BO->getLHS(), BO);
      return true;
    }
    bool VisitUnaryOperator(UnaryOperator *UO) {
      if (UO->isIncrementDecrementOp() ||
          (UO->getOpcode() == UO_AddrOf && !ReadOnlyDecay.contains(UO)))
        write(UO->getSubExpr(), UO);
      return true;
    }
    bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
      // `&g` converted to a pointer-to-const is read through, not written.
      if (const auto *UO = dyn_cast<UnaryOperator>(ICE->getSubExpr()->IgnoreParens());
          UO && UO->getOpcode() == UO_AddrOf && ICE->getType()->isPointerType() &&
          ICE->getType()->getPointeeType().isConstQualified())
        ReadOnlyDecay.insert(UO);
      // An array decayed to a pointer-to-const is read, not written.
      if (const auto *Sub = dyn_cast<ImplicitCastExpr>(ICE->getSubExpr()->IgnoreParens());
          Sub && Sub->getCastKind() == CK_ArrayToPointerDecay &&
          ICE->getType()->isPointerType() &&
          ICE->getType()->getPointeeType().isConstQualified())
        ReadOnlyDecay.insert(Sub);
      if (ICE->getCastKind() == CK_ArrayToPointerDecay &&
          !ReadOnlyDecay.contains(ICE) &&
          !ICE->getType()->getPointeeType().isConstQualified())
        write(ICE->getSubExpr(), ICE);
      return true;
    }
    // `a[i]` decays a to index it: a read, unless the element is written,
    // which the assignment or ++ sees. (Parents are visited first.)
    bool VisitArraySubscriptExpr(ArraySubscriptExpr *ASE) {
      if (const auto *D = dyn_cast<ImplicitCastExpr>(ASE->getBase()->IgnoreParens());
          D && D->getCastKind() == CK_ArrayToPointerDecay)
        ReadOnlyDecay.insert(D);
      return true;
    }
  } F;
  F.TraverseDecl(S.Context.getTranslationUnitDecl());
  if (!SafeCSharedGlobals)
    return;
  for (auto [VD, At] : F.AllWrites)
    if (Unsynchronized(VD) && !SM.isInSystemHeader(At->getExprLoc()) &&
        !S.isInSafeCUnsafeRegion(At->getExprLoc()))
      S.SafeCDiag(At->getExprLoc(), "unsynchronized-write") << VD;
  for (const VarDecl *VD : F.Globals) {
    if (SM.isInSystemHeader(VD->getLocation()) || Shareable(VD) ||
        Unsynchronized(VD))
      continue;
    const Expr *W = F.Written.lookup(VD->getCanonicalDecl());
    bool Internal = !VD->isExternallyVisible();
    if (Internal && !W)
      continue; // never written: a constant
    S.SafeCDiag(VD->getLocation(), "global-unshared") << VD;
    if (W)
      S.SafeCDiag(W->getExprLoc(), "global-unshared-write") << VD;
  }
}
