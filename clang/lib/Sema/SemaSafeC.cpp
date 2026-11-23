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
#include "llvm/ADT/SmallPtrSet.h"
#include "clang/Basic/DiagnosticSema.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/Sema.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CommandLine.h"

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
     "'__safec_returns_owned')"},
    {"storage-borrow", false,
     "%select{field|global}0 %1 holds a pointer nothing tracks, so it can "
     "outlive what it points to; declare it '__safec_owned' if it is "
     "responsible for freeing its target, or '__safec_static' if it only "
     "points at string literals, globals or functions%select{, or mark the "
     "struct '__safec_view' if it lives only in locals and parameters|}0"},
    {"static-store", false,
     "%0 is '__safec_static', so it may point only at string literals, "
     "globals or functions, which are never freed; this value may point at "
     "memory that is; make %0 '__safec_owned' if it is responsible for "
     "freeing what it points to"},
    {"unchecked-read", false,
     "%0 is '__safec_unchecked': nothing tracks whether its target is still "
     "alive, so its value can be used only inside '__unsafe' (or copied into "
     "another '__safec_unchecked'); if this struct owns the target, make %0 "
     "'__safec_owned' instead"},
    {"view-storage", false,
     "%select{global|field}0 %1 holds a '__safec_view' struct, whose borrows "
     "are tracked only in locals and parameters; make it a local, or pass it "
     "as a parameter"},
    {"view-write-through-pointer", false,
     "writing a borrow into a '__safec_view' through a pointer can outlive "
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

static llvm::cl::opt<bool>
    SafeCOwnership("safe-c-ownership",
                   llvm::cl::desc("Safe C: enforce the M2 ownership rules"),
                   llvm::cl::init(false));

bool clang::safeCOwnershipEnabled(const Sema &S) {
  return S.getLangOpts().SafeC && SafeCOwnership;
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

static bool isViewType(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  const RecordDecl *RD = T->getAsRecordDecl();
  return RD && hasSafeCAnnotation(RD, "safec_view");
}

void clang::checkSafeCPointerStorage(Sema &S, Decl *D) {
  if (!safeCOwnershipEnabled(S) ||
      S.getSourceManager().isInSystemHeader(D->getLocation()))
    return;
  auto Kinded = [](const Decl *X) {
    return hasSafeCAnnotation(X, "safec_owned") ||
           hasSafeCAnnotation(X, "safec_static") ||
           hasSafeCAnnotation(X, "safec_unchecked");
  };
  if (auto *RD = dyn_cast<RecordDecl>(D)) {
    if (hasSafeCAnnotation(RD, "safec_view"))
      return;
    for (const FieldDecl *FD : RD->fields()) {
      if (holdsBorrow(FD->getType()) && !Kinded(FD))
        S.SafeCDiag(FD->getLocation(), "storage-borrow") << 0 << FD;
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
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    return DRE->getDecl();
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return ME->getMemberDecl();
  return nullptr;
}

/// E's value is the address of static data: a string literal, a global or
/// part of one, a function, null, or another __safec_static.
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
      return D && hasSafeCAnnotation(D, "safec_static");
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
    if (hasSafeCAnnotation(Dst, "safec_static") &&
        !isStaticAddress(S.Context, Src))
      S.SafeCDiag(Src->getExprLoc(), "static-store") << Dst;
    if (hasSafeCAnnotation(Dst, "safec_unchecked"))
      allowRead(Src);
  }

public:
  explicit PointerKindChecker(Sema &S) : S(S) {}

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->getOpcode() == BO_Assign) {
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
      if (RD->isUnion() && ILE->getInitializedFieldInUnion() != FD)
        continue;
      checkStore(FD, ILE->getInit(I++), ILE->getBeginLoc());
    }
    return true;
  }

  bool VisitImplicitCastExpr(ImplicitCastExpr *ICE) {
    if (ICE->getCastKind() != CK_LValueToRValue)
      return true;
    const ValueDecl *D = namedStorage(ICE->getSubExpr());
    if (D && hasSafeCAnnotation(D, "safec_unchecked") &&
        !AllowedUncheckedReads.count(ICE))
      S.SafeCDiag(ICE->getExprLoc(), "unchecked-read") << D;
    return true;
  }
};
} // namespace

void clang::checkSafeCPointerKindsIn(Sema &S, Stmt *Body) {
  if (Body && safeCOwnershipEnabled(S))
    PointerKindChecker(S).TraverseStmt(Body);
}

void clang::checkSafeCPointerKindsIn(Sema &S, Decl *GlobalVar) {
  if (GlobalVar && safeCOwnershipEnabled(S))
    PointerKindChecker(S).TraverseDecl(GlobalVar);
}
