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

#include "clang/AST/Attr.h"
#include "clang/AST/ContractSpecifier.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Sema.h"

using namespace clang;

/// Returns the callee of the first call in \p E that is not marked 'const' or
/// 'pure', or null if the impurity does not come from a call.
///
/// The call is usually nested (`impure(n) > 0`), so the whole expression has to
/// be searched rather than just its top node.
static const FunctionDecl *findImpureCallee(const Stmt *E) {
  if (const auto *CE = dyn_cast<CallExpr>(E))
    if (const FunctionDecl *Callee = CE->getDirectCallee())
      if (!Callee->hasAttr<ConstAttr>() && !Callee->hasAttr<PureAttr>())
        return Callee;

  for (const Stmt *Child : E->children())
    if (Child)
      if (const FunctionDecl *Callee = findImpureCallee(Child))
        return Callee;

  return nullptr;
}

ExprResult Sema::ActOnContractClausePredicate(ContractClause::ClauseKind Kind,
                                              SourceLocation KeywordLoc,
                                              Expr *Predicate) {
  if (!Predicate)
    return ExprError();

  // A predicate is a condition, not a value: the same contextual conversion
  // that 'if' applies, so `pre (p)` on a pointer means `pre (p != 0)` and a
  // struct-valued predicate is rejected with the usual diagnostic.
  ExprResult Cond = CheckBooleanCondition(KeywordLoc, Predicate);
  if (Cond.isInvalid())
    return Cond;

  // Predicates must be pure. A contract that mutates state cannot be evaluated
  // twice, and every tier wants to evaluate it more than once: the runtime
  // check at entry, the static checker at each call site, and the CBMC export.
  //
  // HasSideEffects already treats a call to anything not marked 'const' or
  // 'pure' as a possible effect, so those existing attributes serve as the
  // "usable in specs" marker rather than a new one being invented here.
  if (Cond.get()->HasSideEffects(Context)) {
    Diag(Cond.get()->getExprLoc(), diag::err_contract_predicate_not_pure)
        << Cond.get()->getSourceRange();
    if (const FunctionDecl *Callee = findImpureCallee(Cond.get()))
      Diag(Callee->getLocation(), diag::note_contract_predicate_pure_call)
          << Callee;
    return ExprError();
  }

  return Cond;
}

void Sema::ActOnFunctionContracts(Declarator &D, FunctionDecl *FD) {
  if (!D.hasContractClauses() || !FD)
    return;

  // Restating contracts on a redeclaration would mean comparing two predicates
  // written against different ParmVarDecls for equivalence. That is not
  // implemented, and silently keeping one of the two would make which
  // declaration a caller happened to see change what gets checked. Reject it
  // instead, which also lets getContractsForCall assume at most one carrier.
  for (const FunctionDecl *Prev : FD->redecls()) {
    if (Prev == FD || !Prev->hasContracts())
      continue;
    Diag(D.getContractClauses().front().getKeywordLoc(),
         diag::err_contracts_on_redeclaration)
        << D.getContractClauses().front().getSourceRange();
    Diag(Prev->getContracts()->clauses().front().getKeywordLoc(),
         diag::note_previous_declaration);
    return;
  }

  FD->setContracts(ContractSpecifier::Create(Context, D.getContractClauses()));
}

/// Returns the first parameter named anywhere in \p E, or null.
static const ParmVarDecl *findReferencedParameter(const Stmt *E) {
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      return PVD;

  for (const Stmt *Child : E->children())
    if (Child)
      if (const ParmVarDecl *PVD = findReferencedParameter(Child))
        return PVD;

  return nullptr;
}

ExprResult Sema::CheckContractPostPredicate(Expr *Predicate) {
  const ParmVarDecl *PVD = findReferencedParameter(Predicate);
  if (!PVD)
    return Predicate;

  Diag(Predicate->getExprLoc(), diag::err_contract_post_names_parameter)
      << PVD << Predicate->getSourceRange();
  Diag(PVD->getLocation(), diag::note_contract_old_unimplemented);
  return ExprError();
}

void Sema::DiagnoseContractsOnNonFunction(Declarator &D) {
  if (!D.hasContractClauses())
    return;
  Diag(D.getContractClauses().front().getKeywordLoc(),
       diag::err_contracts_on_non_function)
      << D.getContractClauses().front().getSourceRange();
}
