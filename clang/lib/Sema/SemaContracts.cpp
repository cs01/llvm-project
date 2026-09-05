//===- SemaContracts.cpp - Semantic analysis for C contracts --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// Implements semantic analysis for the contract clauses parsed under
/// -fc-contracts.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ContractSpecifier.h"
#include "clang/AST/Decl.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Sema.h"

using namespace clang;

ExprResult Sema::ActOnContractClausePredicate(ContractClause::ClauseKind Kind,
                                              SourceLocation KeywordLoc,
                                              Expr *Predicate) {
  if (!Predicate)
    return ExprError();

  // A predicate is a condition, not a value: the same contextual conversion
  // that 'if' applies, so `pre (p)` on a pointer means `pre (p != 0)` and a
  // struct-valued predicate is rejected with the usual diagnostic.
  return CheckBooleanCondition(KeywordLoc, Predicate);
}

void Sema::ActOnFunctionContracts(Declarator &D, FunctionDecl *FD) {
  if (!D.hasContractClauses() || !FD)
    return;
  FD->setContracts(ContractSpecifier::Create(Context, D.getContractClauses()));
}

void Sema::DiagnoseContractsOnNonFunction(Declarator &D) {
  if (!D.hasContractClauses())
    return;
  Diag(D.getContractClauses().front().getKeywordLoc(),
       diag::err_contracts_on_non_function)
      << D.getContractClauses().front().getSourceRange();
}
