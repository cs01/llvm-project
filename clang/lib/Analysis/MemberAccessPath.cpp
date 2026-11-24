//===- MemberAccessPath.cpp - A variable and the fields below it ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/MemberAccessPath.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include <algorithm>

using namespace clang;

bool clang::pathHasPrefix(const MemberAccessPath &Path,
                          const MemberAccessPath &Prefix) {
  return Path.Root == Prefix.Root &&
         Path.Fields.size() >= Prefix.Fields.size() &&
         std::equal(Prefix.Fields.begin(), Prefix.Fields.end(),
                    Path.Fields.begin());
}

const Expr *clang::ignoreBoundsSafetyWrappers(const Expr *E) {
  while (true) {
    E = E->IgnoreParenImpCasts();
    if (const auto *OVE = dyn_cast<OpaqueValueExpr>(E)) {
      if (!OVE->getSourceExpr())
        return E;
      E = OVE->getSourceExpr();
    } else if (const auto *MSE = dyn_cast<MaterializeSequenceExpr>(E)) {
      E = MSE->getWrappedExpr();
    } else if (const auto *BCE = dyn_cast<BoundsCheckExpr>(E)) {
      E = BCE->getGuardedExpr();
    } else if (const auto *PPE =
                   dyn_cast<BoundsSafetyPointerPromotionExpr>(E)) {
      E = PPE->getPointer();
    } else {
      return E;
    }
  }
}

std::optional<MemberAccessPath> clang::decomposeMemberAccess(
    const Expr *E, llvm::function_ref<const Expr *(const Expr *)> StepThrough,
    llvm::function_ref<const VarDecl *(const VarDecl *)> RootOf) {
  llvm::SmallVector<const FieldDecl *, 2> Fields;
  E = ignoreBoundsSafetyWrappers(E);
  while (true) {
    if (const auto *ME = dyn_cast<MemberExpr>(E)) {
      const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl());
      if (!FD)
        return std::nullopt;
      Fields.push_back(FD);
      E = ignoreBoundsSafetyWrappers(ME->getBase());
      continue;
    }
    // `(*p).f` names the same place as `p->f`.
    if (const auto *UO = dyn_cast<UnaryOperator>(E);
        UO && UO->getOpcode() == UO_Deref && !Fields.empty()) {
      E = ignoreBoundsSafetyWrappers(UO->getSubExpr());
      continue;
    }
    if (StepThrough && !Fields.empty())
      if (const Expr *Next = StepThrough(E)) {
        E = Next;
        continue;
      }
    break;
  }
  if (Fields.empty())
    return std::nullopt;
  std::reverse(Fields.begin(), Fields.end());
  MemberAccessPath Path;
  Path.Fields = std::move(Fields);
  if (isa<CXXThisExpr>(E)) {
    Path.Root = nullptr;
  } else if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!VD)
      return std::nullopt;
    Path.Root = RootOf ? RootOf(VD) : VD;
  } else {
    return std::nullopt;
  }
  return Path;
}
