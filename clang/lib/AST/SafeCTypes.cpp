//===- SafeCTypes.cpp - what a Safe C type promises --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#include "clang/AST/SafeCTypes.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecordLayout.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "llvm/ADT/StringExtras.h"
#include <functional>

namespace clang::safec {

const Expr *peel(const Expr *E) {
  while (true) {
    E = E->IgnoreParenImpCasts();
    const auto *OVE = dyn_cast<OpaqueValueExpr>(E);
    if (!OVE || !OVE->getSourceExpr())
      return E;
    E = OVE->getSourceExpr();
  }
}

/// What a pointer type promises a caller, beyond its C type: the ownership
/// kind, and its bounds (with the parameters a count names, by position).
std::string promise(QualType T) {
  std::string Out = pointerKind(T).str();
  if (const auto *CAT = T->getAs<CountAttributedType>()) {
    Out += "|count" + std::to_string(int(CAT->getKind())) + ":";
    struct Printer : RecursiveASTVisitor<Printer> {
      std::string Out;
      bool VisitDeclRefExpr(DeclRefExpr *E) {
        if (const auto *P = dyn_cast<ParmVarDecl>(E->getDecl()))
          Out += "#" + std::to_string(P->getFunctionScopeIndex());
        else
          Out += E->getDecl()->getNameAsString();
        Out += ",";
        return true;
      }
      bool VisitIntegerLiteral(IntegerLiteral *E) {
        Out += llvm::toString(E->getValue(), 10, false) + ",";
        return true;
      }
      bool VisitBinaryOperator(BinaryOperator *E) {
        Out += E->getOpcodeStr().str();
        return true;
      }
    } P;
    P.TraverseStmt(CAT->getCountExpr());
    Out += P.Out;
  } else if (T->isBoundsAttributedType()) {
    Out += "|bounds";
  } else if (const auto *VT = T->getAs<ValueTerminatedType>()) {
    // '__null_terminated' and '__terminated_by(0)' are one promise.
    const Expr *E = VT->getTerminatorExpr()->IgnoreParenImpCasts();
    Out += "|terminated";
    if (const auto *I = dyn_cast<IntegerLiteral>(E))
      Out += llvm::toString(I->getValue(), 10, false);
    else if (const auto *C = dyn_cast<CharacterLiteral>(E))
      Out += std::to_string(C->getValue());
    else
      Out += "?";
  }
  if (T->isPointerType() && T->getPointeeType()->isPointerType())
    Out += "*" + promise(T->getPointeeType());
  return Out;
}

static void addRecord(std::string &Out, QualType T, unsigned Depth);
static void addType(std::string &Out, QualType T, unsigned Depth) {
  Out += T.getCanonicalType().getAsString() + promise(T);
  if (auto N = T->getNullability())
    Out += "|null" + std::to_string(int(*N));
  addRecord(Out, T, Depth);
  // What a function pointer's own parameters promise.
  QualType P = T;
  while (P->isPointerType())
    P = P->getPointeeType();
  if (const auto *FT = P->getAs<FunctionProtoType>(); FT && Depth <= 6) {
    Out += "(*";
    addType(Out, FT->getReturnType(), Depth + 1);
    for (QualType A : FT->param_types()) {
      Out += ",";
      addType(Out, A, Depth + 1);
    }
    Out += ")";
  }
}
static void addRecord(std::string &Out, QualType T, unsigned Depth) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  if (const auto *ED = T->getAsEnumDecl())
    if (const EnumDecl *Def = ED->getDefinition()) {
      Out += "=" + Def->getIntegerType().getCanonicalType().getAsString();
      for (const EnumConstantDecl *C : Def->enumerators())
        Out += "," + C->getNameAsString() + "=" +
               llvm::toString(C->getInitVal(), 10);
    }
  const RecordDecl *RD = T->getAsRecordDecl();
  RD = RD ? RD->getDefinition() : nullptr;
  if (!RD || Depth > 6)
    return;
  Out += "{";
  // Packing and alignment, however they were asked for.
  const ASTRecordLayout *L = nullptr;
  if (!RD->isInvalidDecl()) {
    L = &RD->getASTContext().getASTRecordLayout(RD);
    Out += std::to_string(L->getSize().getQuantity()) + "a" +
           std::to_string(L->getAlignment().getQuantity());
  }
  for (const auto *A : RD->specific_attrs<AnnotateAttr>())
    Out += A->getAnnotation().str();
  for (const FieldDecl *F : RD->fields()) {
    Out += F->getNameAsString() + ":";
    if (L)
      Out += std::to_string(L->getFieldOffset(F->getFieldIndex())) + ":";
    if (F->isBitField())
      Out += "b" + std::to_string(F->getBitWidthValue()) + ":";
    addType(Out, F->getType(), Depth + 1);
    for (const auto *A : F->specific_attrs<AnnotateAttr>()) {
      Out += A->getAnnotation().str();
      for (const Expr *Arg : A->args()) {
        Expr::EvalResult R;
        if (Arg->EvaluateAsInt(R, F->getASTContext()))
          Out += llvm::toString(R.Val.getInt(), 10) + ",";
      }
    }
    Out += ";";
  }
  Out += "}";
}

std::vector<std::pair<std::string, std::string>>
pointedStructs(const ValueDecl *VD) {
  std::vector<std::pair<std::string, std::string>> Out;
  llvm::SmallPtrSet<const TagDecl *, 8> Seen;
  // Every struct that can be reached from the symbol's type.
  std::function<void(QualType, bool)> Add = [&](QualType T, bool ByValue) {
    while (T->isPointerType() || T->isArrayType()) {
      ByValue &= !T->isPointerType();
      T = T->isPointerType() ? T->getPointeeType()
                             : QualType(T->getPointeeOrArrayElementType(), 0);
    }
    if (const auto *FT = T->getAs<FunctionProtoType>()) {
      Add(FT->getReturnType(), false);
      for (QualType A : FT->param_types())
        Add(A, false);
      return;
    }
    const TagDecl *TD = T->getAsTagDecl();
    TD = TD ? TD->getDefinition() : nullptr;
    if (!TD || !Seen.insert(TD).second ||
        VD->getASTContext().getSourceManager().isInSystemHeader(
            TD->getLocation()))
      return;
    if (const auto *RD = dyn_cast<RecordDecl>(TD))
      for (const FieldDecl *F : RD->fields())
        Add(F->getType(), true);
    // The type's own description already has what it holds by value.
    if (ByValue)
      return;
    std::string Name = TD->getNameAsString(), Contents;
    if (Name.empty() && TD->getTypedefNameForAnonDecl())
      Name = TD->getTypedefNameForAnonDecl()->getNameAsString();
    addRecord(Contents, T, 0);
    if (!Name.empty())
      Out.push_back({Name, Contents});
  };
  if (const auto *FD = dyn_cast<FunctionDecl>(VD)) {
    Add(FD->getReturnType(), true);
    for (const ParmVarDecl *P : FD->parameters())
      Add(P->getType(), true);
  } else {
    Add(VD->getType(), true);
  }
  return Out;
}

std::string signature(const ValueDecl *VD) {
  std::string Out;
  const auto *FD = dyn_cast<FunctionDecl>(VD);
  if (!FD) {
    addType(Out, VD->getType(), 0);
  } else {
    addType(Out, FD->getReturnType(), 0);
    for (const ParmVarDecl *P : FD->parameters()) {
      Out += "(";
      addType(Out, P->getType(), 0);
      Out += ")";
    }
  }
  // Locks: printed, so the capability expressions count.
  for (const Attr *A : VD->attrs())
    switch (A->getKind()) {
    case attr::GuardedBy:
    case attr::PtGuardedBy:
    case attr::RequiresCapability:
    case attr::AcquireCapability:
    case attr::ReleaseCapability:
    case attr::TryAcquireCapability:
    case attr::AssertCapability:
    case attr::LocksExcluded:
    case attr::ReturnsNonNull:
    case attr::AllocSize: {
      llvm::raw_string_ostream OS(Out);
      A->printPretty(OS, VD->getASTContext().getPrintingPolicy());
      break;
    }
    default:
      break;
    }
  return Out;
}

llvm::StringRef pointerKind(QualType T) {
  while (const ArrayType *AT = T->getAsArrayTypeUnsafe())
    T = AT->getElementType();
  // Walk this pointer's own sugar (typedefs, bounds and nullability
  // attributes), never into its pointee.
  for (const Type *Cur = T.getTypePtr();;) {
    if (const auto *AT = dyn_cast<AttributedType>(Cur)) {
      if (const auto *A = dyn_cast_or_null<AnnotateTypeAttr>(AT->getAttr()))
        if (A->getAnnotation().starts_with("safec_"))
          return A->getAnnotation().drop_front(6);
      Cur = AT->getModifiedType().getTypePtr();
      continue;
    }
    QualType Next = Cur->getLocallyUnqualifiedSingleStepDesugaredType();
    if (Next.getTypePtr() == Cur)
      return "";
    Cur = Next.getTypePtr();
  }
}

const VarDecl *rootVar(const Expr *E) {
  while (true) {
    E = peel(E);
    if (const auto *ME = dyn_cast<MemberExpr>(E))
      E = ME->getBase();
    else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
      E = ASE->getBase();
    else if (const auto *UO = dyn_cast<UnaryOperator>(E);
             UO && (UO->getOpcode() == UO_Deref || UO->getOpcode() == UO_AddrOf))
      E = UO->getSubExpr();
    else
      break;
  }
  const auto *DRE = dyn_cast<DeclRefExpr>(E);
  const auto *VD = DRE ? dyn_cast<VarDecl>(DRE->getDecl()) : nullptr;
  return VD && !VD->hasGlobalStorage() ? VD : nullptr;
}

} // namespace clang::safec
