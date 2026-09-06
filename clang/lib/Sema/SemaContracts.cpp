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
#include "clang/Basic/SourceManager.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"

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

/// Replaces whole-token occurrences of \p From with \p To in \p S.
///
/// Substring replacement would corrupt an identifier that merely contains the
/// result name, so both neighbours have to be checked for identifier-ness.
static std::string replaceToken(StringRef S, StringRef From, StringRef To) {
  auto IsIdentChar = [](char C) { return isAlphanumeric(C) || C == '_'; };
  std::string Out;
  size_t Pos = 0;
  while (Pos < S.size()) {
    size_t Found = S.find(From, Pos);
    if (Found == StringRef::npos) {
      Out += S.substr(Pos).str();
      break;
    }
    bool LeftOK = Found == 0 || !IsIdentChar(S[Found - 1]);
    size_t End = Found + From.size();
    bool RightOK = End >= S.size() || !IsIdentChar(S[End]);
    Out += S.substr(Pos, Found - Pos).str();
    Out += (LeftOK && RightOK) ? To.str() : From.str();
    Pos = End;
  }
  return Out;
}

/// Prints one 'assigns' clause as `__CPROVER_assigns(a, b, ...)`.
///
/// A frame condition is a list of locations rather than a predicate, so it
/// prints from the target list. Shared by the function and loop printers: the
/// clause means the same thing in both places.
static void printCProverAssigns(const ContractClause &Clause,
                                const ASTContext &Ctx) {
  llvm::outs() << "__CPROVER_assigns(";
  bool First = true;
  for (const Expr *Target : Clause.getTargets()) {
    if (!First)
      llvm::outs() << ", ";
    First = false;
    Target->printPretty(llvm::outs(), nullptr, Ctx.getPrintingPolicy());
  }
  llvm::outs() << ")\n";
}

/// Prints \p FD's contracts as CBMC function-contract clauses.
///
/// The mapping is close to one to one, which is the argument for targeting CBMC
/// rather than building a verifier: `pre` is `__CPROVER_requires`, `post` is
/// `__CPROVER_ensures` with the result binding renamed to
/// `__CPROVER_return_value`, and `old` is `__CPROVER_old`.
static void printCProverContracts(const FunctionDecl *FD,
                                  const ASTContext &Ctx) {
  const ContractSpecifier *CS = FD->getContracts();
  if (!CS)
    return;

  llvm::outs() << "/* " << FD->getNameAsString() << " */\n";
  for (const ContractClause &Clause : *CS) {
    if (Clause.isInvalid())
      continue;

    // An 'assigns' has targets instead of a predicate, so there is nothing to
    // pretty-print here; its case below prints from the target list.
    std::string Text;
    if (const Expr *P = Clause.getPredicate()) {
      llvm::raw_string_ostream OS(Text);
      P->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
    }

    switch (Clause.getKind()) {
    case ContractClause::CK_Pre:
      // No 'old' rewrite here. 'old()' is rejected outside 'ensures', so an
      // 'old' token in a requires is an ordinary identifier: rewriting it
      // turned `requires (old > 0)` on a parameter named 'old' into
      // `__CPROVER_requires(__CPROVER_old > 0)`, which is silently wrong
      // rather than an error.
      llvm::outs() << "__CPROVER_requires(" << Text << ")\n";
      break;
    case ContractClause::CK_Post:
      // Our StmtPrinter spells the node `old(...)`; CBMC spells it
      // `__CPROVER_old(...)`. Only an 'ensures' can contain one.
      //
      // FIXME: still token substitution, so a parameter named 'old' referenced
      // as `old(old)` is rewritten on both sides. Printing the ContractOldExpr
      // node directly, via a printing policy, is the real fix.
      Text = replaceToken(Text, "old", "__CPROVER_old");
      if (const VarDecl *R = Clause.getResultVar())
        Text = replaceToken(Text, R->getName(), "__CPROVER_return_value");
      llvm::outs() << "__CPROVER_ensures(" << Text << ")\n";
      break;
    case ContractClause::CK_Assigns:
      printCProverAssigns(Clause, Ctx);
      break;
    case ContractClause::CK_LoopInvariant:
    case ContractClause::CK_Decreases:
      // Loop clauses. They hang off a statement, so they are never reachable
      // from a FunctionDecl's contracts; printCProverLoopContracts prints them.
      break;
    }
  }
}

/// Prints the loop contracts reachable from \p S as CBMC loop-contract clauses.
///
/// `invariant` is `__CPROVER_loop_invariant` and `variant` is
/// `__CPROVER_decreases`. Unlike `pre` and `post`, these hang off a statement
/// inside the body rather than off the declarator, so they have to be walked
/// for rather than read off the FunctionDecl.
static void printCProverLoopContracts(const Stmt *S, const FunctionDecl *FD,
                                      const ASTContext &Ctx) {
  if (!S)
    return;

  if (const ContractSpecifier *CS = Ctx.getLoopContracts(S)) {
    const char *Keyword = isa<WhileStmt>(S) ? "while"
                          : isa<ForStmt>(S) ? "for"
                                            : "do";
    PresumedLoc PL = Ctx.getSourceManager().getPresumedLoc(S->getBeginLoc());
    llvm::outs() << "/* " << FD->getNameAsString() << ": " << Keyword
                 << " at line " << (PL.isValid() ? PL.getLine() : 0);

    // goto-instrument takes loop contracts on 'while' and 'for' only; on a 'do'
    // it rejects them outright, which proofs/zstd hit by hand. The fix is the
    // mechanical do { B } while (C) => while (1) { B; if (!C) break; } rewrite,
    // but applying it here would mean emitting rewritten source rather than
    // clauses, which is not what this mode does. Say so instead of printing
    // clauses that CBMC will refuse without explanation.
    if (isa<DoStmt>(S))
      llvm::outs() << "; needs the do => while (1) { B; if (!C) break; }"
                      " rewrite before goto-instrument accepts these";
    llvm::outs() << " */\n";

    for (const ContractClause &Clause : *CS) {
      if (Clause.isInvalid())
        continue;

      // No 'old' substitution here, unlike the function clauses: 'old()' is
      // rejected outside 'post', so an 'old' token in a loop clause is an
      // ordinary identifier and rewriting it would corrupt the predicate.
      std::string Text;
      if (const Expr *P = Clause.getPredicate()) {
        llvm::raw_string_ostream OS(Text);
        P->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
      }

      switch (Clause.getKind()) {
      case ContractClause::CK_LoopInvariant:
        llvm::outs() << "__CPROVER_loop_invariant(" << Text << ")\n";
        break;
      case ContractClause::CK_Decreases:
        llvm::outs() << "__CPROVER_decreases(" << Text << ")\n";
        break;
      case ContractClause::CK_Assigns:
        printCProverAssigns(Clause, Ctx);
        break;
      case ContractClause::CK_Pre:
      case ContractClause::CK_Post:
        // Not parseable on a loop.
        break;
      }
    }
  }

  for (const Stmt *Child : S->children())
    printCProverLoopContracts(Child, FD, Ctx);
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

void Sema::EmitCProverContracts(const FunctionDecl *FD) {
  // Called after the delayed 'post' predicates have been replayed, since until
  // then those clauses have no predicate to print.
  if (getLangOpts().CContractsEmitCProver && FD)
    printCProverContracts(FD, Context);
}

void Sema::EmitCProverLoopContracts(const Decl *D) {
  if (!getLangOpts().CContractsEmitCProver || !D)
    return;
  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD || !FD->hasBody())
    return;
  printCProverLoopContracts(FD->getBody(), FD, Context);
}

ExprResult Sema::BuildContractOldExpr(SourceLocation OldLoc,
                                      SourceLocation LParenLoc,
                                      SourceLocation RParenLoc, Expr *SubExpr) {
  if (!SubExpr)
    return ExprError();

  ExprResult Converted = DefaultFunctionArrayLvalueConversion(SubExpr);
  if (Converted.isInvalid())
    return ExprError();
  SubExpr = Converted.get();

  if (!SubExpr->getType()->isScalarType()) {
    Diag(OldLoc, diag::err_contract_old_not_scalar)
        << SubExpr->getType() << SubExpr->getSourceRange();
    return ExprError();
  }

  return new (Context) ContractOldExpr(OldLoc, LParenLoc, RParenLoc, SubExpr);
}

/// Returns the first parameter named anywhere in \p E outside an `old`, or
/// null.
///
/// Descent stops at a ContractOldExpr: naming a parameter there is exactly the
/// supported way to do it, so those references are not the ambiguous ones.
static const DeclRefExpr *findBareParameterRef(const Stmt *E) {
  if (isa<ContractOldExpr>(E))
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (isa<ParmVarDecl>(DRE->getDecl()))
      return DRE;

  for (const Stmt *Child : E->children())
    if (Child)
      if (const DeclRefExpr *DRE = findBareParameterRef(Child))
        return DRE;

  return nullptr;
}

ExprResult Sema::CheckContractPostPredicate(Expr *Predicate) {
  const DeclRefExpr *DRE = findBareParameterRef(Predicate);
  if (!DRE)
    return Predicate;

  const auto *PVD = cast<ParmVarDecl>(DRE->getDecl());
  Diag(DRE->getLocation(), diag::err_contract_post_names_parameter)
      << PVD << DRE->getSourceRange();
  Diag(DRE->getLocation(), diag::note_contract_post_use_old) << PVD->getName();
  return ExprError();
}

ExprResult Sema::ActOnContractAssignsTarget(Expr *Target) {
  if (!Target)
    return ExprError();

  // A frame target names a location, so unlike a predicate it is not converted
  // to bool and keeps its own type. What it must be is an lvalue:
  // `assigns (n)` on a parameter, `assigns (*p)`, `assigns (buf[i])`,
  // `assigns (s->field)`. A value like `assigns (n + 1)` names nothing that
  // could be written to.
  if (!Target->isLValue()) {
    Diag(Target->getExprLoc(), diag::err_contract_assigns_not_lvalue)
        << Target->getSourceRange();
    return ExprError();
  }

  // Evaluating a frame target must not change the state it is describing.
  if (Target->HasSideEffects(Context)) {
    Diag(Target->getExprLoc(), diag::err_contract_predicate_not_pure)
        << Target->getSourceRange();
    return ExprError();
  }

  return Target;
}

void Sema::ActOnContractAssignsClause(ContractClause &Clause,
                                      ArrayRef<Expr *> Targets) {
  // `assigns ()` is the empty frame: the function modifies nothing. That is a
  // real specification rather than an error, so it still gets a non-null array,
  // which is what lets isInvalid() tell it apart from a parse failure.
  Expr **Stored = new (Context) Expr *[Targets.size() + 1];
  std::copy(Targets.begin(), Targets.end(), Stored);
  Clause.setTargets(Stored, Targets.size());
}

ExprResult Sema::ActOnLoopDecreases(SourceLocation KeywordLoc, Expr *Measure) {
  if (!Measure)
    return ExprError();

  ExprResult Converted = DefaultFunctionArrayLvalueConversion(Measure);
  if (Converted.isInvalid())
    return ExprError();
  Measure = Converted.get();

  if (!Measure->getType()->isScalarType()) {
    Diag(KeywordLoc, diag::err_contract_decreases_not_scalar)
        << Measure->getType() << Measure->getSourceRange();
    return ExprError();
  }
  if (Measure->HasSideEffects(Context)) {
    Diag(Measure->getExprLoc(), diag::err_contract_predicate_not_pure)
        << Measure->getSourceRange();
    return ExprError();
  }
  return Measure;
}

void Sema::ActOnLoopContracts(Stmt *S, ArrayRef<ContractClause> Clauses) {
  if (!S || Clauses.empty())
    return;
  Context.setLoopContracts(S, ContractSpecifier::Create(Context, Clauses));
}

void Sema::DiagnoseContractsOnNonFunction(Declarator &D) {
  if (!D.hasContractClauses())
    return;
  Diag(D.getContractClauses().front().getKeywordLoc(),
       diag::err_contracts_on_non_function)
      << D.getContractClauses().front().getSourceRange();
}
