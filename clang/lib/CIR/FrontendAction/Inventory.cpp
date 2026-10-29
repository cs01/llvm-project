//===--- Inventory.cpp - Unsafe construct sites for cir-modernize ---------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Lists the sites in a translation unit where C++ gives up a guarantee that
// Rust's safe subset keeps: raw pointer parameters, pointer arithmetic and raw
// indexing, unchecked casts, manual memory and locks, unions, varargs,
// uninitialized locals. Each site gets a category, the row of the comparison
// table it belongs to, and a state when this translation unit can settle it:
// proven (a fact shows the hazard cannot happen) or unknown. project.ts
// settles parameter sites from the rewrites and totals the project.
//
// Only source the project owns is listed: system headers are skipped, and a
// site written inside a macro is listed once, where the macro is expanded.
//
//===----------------------------------------------------------------------===//

#include "CIRModernizeFramework.h"

#include "clang/AST/RecursiveASTVisitor.h"
#include "llvm/Support/JSON.h"

using namespace clang;
using namespace clang::cirmodernize;

namespace {

struct Site {
  SourceLocation Loc;
  StringRef Category, Row, State, Why;
  std::string Usr;
  int Index = -1;
};

class InventoryVisitor : public RecursiveASTVisitor<InventoryVisitor> {
public:
  explicit InventoryVisitor(Context &Ctx) : Ctx(Ctx), SM(Ctx.SM) {}

  std::vector<Site> Sites;

  bool VisitFunctionDecl(FunctionDecl *FD) {
    if (!FD->doesThisDeclarationHaveABody() || FD->isImplicit())
      return true;
    for (ParmVarDecl *P : FD->parameters()) {
      QualType T = P->getType();
      if (!T->isPointerType() || T->isFunctionPointerType())
        continue;
      if (T->isVoidPointerType()) {
        add(P->getLocation(), "void pointer parameter", "types");
        continue;
      }
      Site S{P->getLocation(), "raw pointer parameter", "null", "unknown"};
      S.Usr = Ctx.usr(FD);
      S.Index = P->getFunctionScopeIndex();
      push(std::move(S));
    }
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator *BO) {
    if (BO->isAdditiveOp() || BO->getOpcode() == BO_AddAssign ||
        BO->getOpcode() == BO_SubAssign)
      if (BO->getLHS()->getType()->isPointerType() ||
          BO->getRHS()->getType()->isPointerType())
        if (!(BO->getOpcode() == BO_Sub &&
              BO->getRHS()->getType()->isPointerType()))
          add(BO->getOperatorLoc(), "pointer arithmetic", "bounds");
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator *UO) {
    if (UO->isIncrementDecrementOp() &&
        UO->getSubExpr()->getType()->isPointerType())
      add(UO->getOperatorLoc(), "pointer arithmetic", "bounds");
    return true;
  }

  bool VisitArraySubscriptExpr(ArraySubscriptExpr *E) {
    const Expr *Base = E->getBase()->IgnoreParenImpCasts();
    const auto *CAT = Ctx.C.getAsConstantArrayType(Base->getType());
    if (!CAT) {
      add(E->getRBracketLoc(), "index through a pointer", "bounds");
      return true;
    }
    Site S{E->getRBracketLoc(), "C array index", "bounds", "unknown"};
    Expr::EvalResult R;
    if (!E->getIdx()->isValueDependent() &&
        E->getIdx()->EvaluateAsInt(R, Ctx.C)) {
      llvm::APSInt I = R.Val.getInt();
      bool InBounds = !I.isNegative() &&
                      I.getExtValue() < (int64_t)CAT->getZExtSize();
      S.State = InBounds ? "proven" : "outside";
      S.Why = InBounds ? "constant index in bounds" : "constant index out of bounds";
    }
    push(std::move(S));
    return true;
  }

  bool VisitVarDecl(VarDecl *VD) {
    if (isa<ParmVarDecl>(VD))
      return true;
    if (Ctx.C.getAsConstantArrayType(VD->getType()))
      add(VD->getLocation(), "C array", "bounds");
    if (VD->isLocalVarDecl() && !VD->isStaticLocal() && !VD->hasInit() &&
        !VD->getType()->isDependentType() &&
        (VD->getType()->isScalarType() ||
         VD->getType().isTrivialType(Ctx.C)))
      add(VD->getLocation(), "uninitialized local", "uninit");
    return true;
  }

  bool VisitFieldDecl(FieldDecl *FD) {
    if (Ctx.C.getAsConstantArrayType(FD->getType()))
      add(FD->getLocation(), "C array", "bounds");
    return true;
  }

  bool VisitRecordDecl(RecordDecl *RD) {
    if (RD->isUnion() && RD->isThisDeclarationADefinition() &&
        !RD->isImplicit())
      add(RD->getLocation(), "union", "types");
    return true;
  }

  bool VisitCStyleCastExpr(CStyleCastExpr *E) {
    cast(E, "C-style cast");
    return true;
  }

  bool VisitCXXFunctionalCastExpr(CXXFunctionalCastExpr *E) {
    cast(E, "C-style cast");
    return true;
  }

  bool VisitCXXReinterpretCastExpr(CXXReinterpretCastExpr *E) {
    add(E->getOperatorLoc(), "reinterpret_cast", "types");
    return true;
  }

  bool VisitCXXConstCastExpr(CXXConstCastExpr *E) {
    add(E->getOperatorLoc(), "const_cast", "types");
    return true;
  }

  bool VisitCXXStaticCastExpr(CXXStaticCastExpr *E) {
    if (E->getCastKind() == CK_BaseToDerived)
      add(E->getOperatorLoc(), "unchecked downcast", "types");
    return true;
  }

  bool VisitCXXNewExpr(CXXNewExpr *E) {
    add(E->getBeginLoc(), "manual new", "memory");
    return true;
  }

  bool VisitCXXDeleteExpr(CXXDeleteExpr *E) {
    add(E->getBeginLoc(), "manual delete", "memory");
    return true;
  }

  bool VisitCallExpr(CallExpr *CE) {
    const FunctionDecl *Callee = CE->getDirectCallee();
    if (!Callee)
      return true;
    switch (Callee->getBuiltinID()) {
    case Builtin::BImemcpy:
    case Builtin::BImemmove:
    case Builtin::BImemset:
    case Builtin::BIstrcpy:
    case Builtin::BIstrncpy:
    case Builtin::BIstrcat:
    case Builtin::BIstrncat:
    case Builtin::BIsprintf:
    case Builtin::BI__builtin_memcpy:
    case Builtin::BI__builtin_memmove:
    case Builtin::BI__builtin_memset:
      add(CE->getBeginLoc(), "raw memory function", "bounds");
      return true;
    default:
      break;
    }
    if (Callee->isVariadic() && !isa<CXXMethodDecl>(Callee)) {
      // -Wformat checks a printf-style call's arguments against its format.
      bool Format = Callee->hasAttr<FormatAttr>();
      add(CE->getBeginLoc(), "variadic call", "types",
          Format ? "covered" : "unknown",
          Format ? "-Wformat checks the arguments" : "");
    }
    if (auto *MC = dyn_cast<CXXMemberCallExpr>(CE)) {
      const CXXRecordDecl *RD = MC->getRecordDecl();
      // A conversion operator or operator() has no identifier.
      const IdentifierInfo *II = Callee->getIdentifier();
      StringRef Name = II ? II->getName() : "";
      if (RD && (Name == "lock" || Name == "unlock" || Name == "try_lock") &&
          RD->getName().contains_insensitive("mutex"))
        add(CE->getBeginLoc(), "manual lock", "races");
    }
    return true;
  }

private:
  void cast(const ExplicitCastExpr *E, StringRef Category) {
    if (E->getType()->isDependentType() || E->getType()->isVoidType())
      return;
    switch (E->getCastKind()) {
    // Value conversions a static_cast spells the same way.
    case CK_NoOp:
    case CK_IntegralCast:
    case CK_IntegralToFloating:
    case CK_FloatingToIntegral:
    case CK_FloatingCast:
    case CK_IntegralToBoolean:
    case CK_FloatingToBoolean:
    case CK_ConstructorConversion:
    case CK_UserDefinedConversion:
    case CK_LValueToRValue:
      add(E->getBeginLoc(), Category, "types", "unknown",
          "value conversion (static_cast)");
      return;
    default:
      add(E->getBeginLoc(), Category, "types", "unknown",
          E->getCastKindName());
    }
  }

  void add(SourceLocation Loc, StringRef Category, StringRef Row,
           StringRef State = "unknown", StringRef Why = "") {
    push(Site{Loc, Category, Row, State, Why});
  }

  void push(Site S) {
    S.Loc = SM.getExpansionLoc(S.Loc);
    if (S.Loc.isInvalid() || SM.isInSystemHeader(S.Loc))
      return;
    Sites.push_back(std::move(S));
  }

  Context &Ctx;
  SourceManager &SM;
};

} // namespace

void clang::cirmodernize::exportInventory(llvm::json::OStream &J,
                                          Context &Ctx) {
  InventoryVisitor V(Ctx);
  V.TraverseDecl(Ctx.C.getTranslationUnitDecl());
  J.attributeArray("sites", [&] {
    for (const Site &S : V.Sites) {
      auto [FID, Offset] = Ctx.SM.getDecomposedLoc(S.Loc);
      OptionalFileEntryRef FE = Ctx.SM.getFileEntryRefForID(FID);
      if (!FE)
        continue;
      StringRef Path = FE->getFileEntry().tryGetRealPathName();
      J.object([&] {
        J.attribute("file", Path.empty() ? FE->getName() : Path);
        J.attribute("offset", Offset);
        J.attribute("line", Ctx.SM.getExpansionLineNumber(S.Loc));
        J.attribute("category", S.Category);
        J.attribute("row", S.Row);
        J.attribute("state", S.State);
        if (!S.Why.empty())
          J.attribute("why", S.Why);
        if (S.Index >= 0) {
          J.attribute("usr", S.Usr);
          J.attribute("index", S.Index);
        }
      });
    }
  });
}
