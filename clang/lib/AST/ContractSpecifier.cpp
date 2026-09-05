//===- ContractSpecifier.cpp - C contract clauses
//--------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ContractSpecifier.h"
#include "clang/AST/ASTContext.h"
#include <memory>

using namespace clang;

ContractSpecifier *ContractSpecifier::Create(const ASTContext &Ctx,
                                             ArrayRef<ContractClause> Clauses) {
  if (Clauses.empty())
    return nullptr;

  ContractClause *Storage = Ctx.Allocate<ContractClause>(Clauses.size());
  std::uninitialized_copy(Clauses.begin(), Clauses.end(), Storage);
  return new (Ctx) ContractSpecifier(Storage, Clauses.size());
}
