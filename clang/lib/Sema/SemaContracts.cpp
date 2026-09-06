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
#include "clang/Lex/Lexer.h"
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

/// Renders one clause as the CBMC text for it, without a trailing newline.
///
/// Returns the empty string for a clause with nothing to say. Both consumers go
/// through here: -fcontract-emit-cprover prints the result, and
/// -fcontract-emit-cprover-unit splices it over the original clause, so the two
/// modes cannot drift apart.
static std::string formatCProverClause(const ContractClause &Clause,
                                       const ASTContext &Ctx) {
  if (Clause.isInvalid())
    return {};

  if (Clause.getKind() == ContractClause::CK_Assigns) {
    auto Print = [&](const Expr *E) {
      std::string T;
      llvm::raw_string_ostream TS(T);
      E->printPretty(TS, nullptr, Ctx.getPrintingPolicy());
      return T;
    };

    std::string Out = "__CPROVER_assigns(";
    bool First = true;
    for (const AssignsTarget &Target : Clause.getTargets()) {
      if (!First)
        Out += ", ";
      First = false;

      if (!Target.isRange()) {
        Out += Print(Target.Base);
        continue;
      }

      // `buf[lo : hi]` is a half-open range of *elements*. CBMC's primitive
      // counts *bytes* from a pointer, so the conversion is this compiler's
      // job: the base advances by lo, and the extent is (hi - lo) elements
      // scaled by the element size. Doing that multiply by hand is how a frame
      // ends up smaller than the loop that writes it, which does not fail —
      // it silently proves less.
      std::string Base = Print(Target.Base);
      std::string Hi = Print(Target.Upper);

      // Simplify aggressively. These are identities, but CBMC carries the
      // extent expression symbolically into the formula it solves, so `(p + 0)`
      // and `* sizeof(char)` are not free: the hand-written frame this was
      // checked against discharges in one iteration, and the unsimplified form
      // of the same frame did not finish in fifty minutes.
      llvm::APSInt LowerVal;
      bool LowerIsZero =
          !Target.Lower ||
          (Target.Lower->isIntegerConstantExpr(Ctx) &&
           (LowerVal = Target.Lower->EvaluateKnownConstInt(Ctx)) == 0);

      std::string Ptr =
          LowerIsZero ? Base : "(" + Base + " + " + Print(Target.Lower) + ")";
      std::string Count =
          LowerIsZero ? Hi
                      : "((" + Hi + ") - (" + Print(Target.Lower) + "))";

      // A byte-sized element makes the scale factor the identity too, and byte
      // buffers are most of what a C frame condition ever names.
      QualType Elem = Target.Base->getType()->getPointeeType();
      bool ByteSized =
          !Elem.isNull() && !Elem->isIncompleteType() &&
          Ctx.getTypeSizeInChars(Elem).isOne();
      if (!ByteSized) {
        // One element scaled by its own size is just that size, and a
        // single-element frame ('assigns (p[0 : 1])' for an out-parameter) is
        // common enough to be worth the special case.
        llvm::APSInt CountVal;
        bool CountIsOne =
            Target.Upper->isIntegerConstantExpr(Ctx) &&
            (CountVal = Target.Upper->EvaluateKnownConstInt(Ctx)) == 1;
        Count = (LowerIsZero && CountIsOne)
                    ? "sizeof(*" + Base + ")"
                    : Count + " * sizeof(*" + Base + ")";
      }

      Out += "__CPROVER_object_upto(" + Ptr + ", " + Count + ")";
    }
    return Out + ")";
  }

  const Expr *P = Clause.getPredicate();
  if (!P)
    return {};
  std::string Text;
  {
    llvm::raw_string_ostream OS(Text);
    P->printPretty(OS, nullptr, Ctx.getPrintingPolicy());
  }

  switch (Clause.getKind()) {
  case ContractClause::CK_Pre:
    // No 'old' rewrite here. 'old()' is rejected outside 'post', so an 'old'
    // token in a pre is an ordinary identifier: rewriting it turned
    // `pre (old > 0)` on a parameter named 'old' into
    // `__CPROVER_requires(__CPROVER_old > 0)`, silently wrong rather than an
    // error.
    return "__CPROVER_requires(" + Text + ")";
  case ContractClause::CK_Post:
    // Our StmtPrinter spells the node `old(...)`; CBMC spells it
    // `__CPROVER_old(...)`. Only a 'post' can contain one.
    //
    // FIXME: still token substitution, so a parameter named 'old' referenced as
    // `old(old)` is rewritten on both sides. Printing the ContractOldExpr node
    // via a printing policy is the real fix.
    Text = replaceToken(Text, "old", "__CPROVER_old");
    if (const VarDecl *R = Clause.getResultVar())
      Text = replaceToken(Text, R->getName(), "__CPROVER_return_value");
    return "__CPROVER_ensures(" + Text + ")";
  case ContractClause::CK_LoopInvariant:
    // No 'old' rewrite on loop clauses either, for the same reason.
    return "__CPROVER_loop_invariant(" + Text + ")";
  case ContractClause::CK_Decreases:
    return "__CPROVER_decreases(" + Text + ")";
  case ContractClause::CK_Assigns:
    llvm_unreachable("handled above");
  }
  llvm_unreachable("unhandled contract clause kind");
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
    std::string Text = formatCProverClause(Clause, Ctx);
    if (!Text.empty())
      llvm::outs() << Text << "\n";
  }
}

/// Walks the loop contracts reachable from \p S, handing each clause to \p
/// OnClause.
///
/// Both modes need the same walk: loop clauses hang off statements inside the
/// body rather than off the FunctionDecl, so they have to be found rather than
/// read off a list.
static void forEachLoopContract(
    const Stmt *S, const ASTContext &Ctx,
    llvm::function_ref<void(const Stmt *, const ContractSpecifier &)> OnLoop) {
  if (!S)
    return;
  if (const ContractSpecifier *CS = Ctx.getLoopContracts(S))
    OnLoop(S, *CS);
  for (const Stmt *Child : S->children())
    forEachLoopContract(Child, Ctx, OnLoop);
}

/// Prints the loop contracts reachable from \p S as CBMC loop-contract clauses.
static void printCProverLoopContracts(const Stmt *S, const FunctionDecl *FD,
                                      const ASTContext &Ctx) {
  forEachLoopContract(S, Ctx, [&](const Stmt *L, const ContractSpecifier &CS) {
    const char *Keyword = isa<WhileStmt>(L) ? "while"
                          : isa<ForStmt>(L) ? "for"
                                            : "do";
    PresumedLoc PL = Ctx.getSourceManager().getPresumedLoc(L->getBeginLoc());
    llvm::outs() << "/* " << FD->getNameAsString() << ": " << Keyword
                 << " at line " << (PL.isValid() ? PL.getLine() : 0);

    // goto-instrument takes loop contracts on 'while' and 'for' only; on a 'do'
    // it rejects them outright, which proofs/zstd hit by hand. The mechanical
    // do { B } while (C) => while (1) { B; if (!C) break; } rewrite fixes it,
    // but this mode emits clauses rather than restructured source. Say so
    // instead of printing clauses CBMC will refuse without explanation.
    if (isa<DoStmt>(L))
      llvm::outs() << "; needs the do => while (1) { B; if (!C) break; }"
                      " rewrite before goto-instrument accepts these";
    llvm::outs() << " */\n";

    for (const ContractClause &Clause : CS) {
      std::string Text = formatCProverClause(Clause, Ctx);
      if (!Text.empty())
        llvm::outs() << Text << "\n";
    }
  });
}

/// Records the CBMC replacement for every clause in \p CS.
static void recordCProverRewrites(
    const ContractSpecifier &CS, const ASTContext &Ctx,
    SmallVectorImpl<std::pair<SourceRange, std::string>> &Out) {
  for (const ContractClause &Clause : CS) {
    std::string Text = formatCProverClause(Clause, Ctx);
    if (!Text.empty())
      Out.emplace_back(Clause.getSourceRange(), std::move(Text));
  }
}

void Sema::EmitCProverUnit() {
  if (!getLangOpts().CContractsEmitCProverUnit)
    return;

  SourceManager &SM = getSourceManager();
  FileID Main = SM.getMainFileID();
  StringRef Buf = SM.getBufferData(Main);

  // Offset order, so the splice is one forward pass. Clauses are discovered in
  // parse order, which is not the same thing once a 'post' has been replayed
  // from cached tokens after the body it precedes.
  struct Edit {
    unsigned Begin, End;
    std::string Text;
  };
  SmallVector<Edit, 8> Edits;
  unsigned Skipped = 0;
  for (const auto &R : CProverUnitRewrites) {
    if (!R.first.isValid())
      continue;
    std::pair<FileID, unsigned> B = SM.getDecomposedLoc(R.first.getBegin());
    std::pair<FileID, unsigned> E = SM.getDecomposedLoc(
        Lexer::getLocForEndOfToken(R.first.getEnd(), 0, SM, getLangOpts()));
    // A clause reached through a macro expansion, or declared in an included
    // header, has no span in the main file to replace. Leaving it alone is
    // right; doing so silently is not, because contracts belong in headers and
    // the result would be a translation unit that quietly proves less than the
    // author wrote.
    if (B.first != Main || E.first != Main || E.second < B.second) {
      ++Skipped;
      continue;
    }
    Edits.push_back({B.second, E.second, R.second});
  }
  if (Skipped)
    Diag(SM.getLocForStartOfFile(Main),
         diag::warn_contract_cprover_unit_skipped_header)
        << Skipped;
  llvm::sort(Edits,
             [](const Edit &A, const Edit &B) { return A.Begin < B.Begin; });

  unsigned Pos = 0;
  for (const Edit &E : Edits) {
    if (E.Begin < Pos)
      continue; // Overlapping spans: keep the first, which is the outer one.
    llvm::outs() << Buf.substr(Pos, E.Begin - Pos) << E.Text;
    Pos = E.End;
  }
  llvm::outs() << Buf.substr(Pos);
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
  if (!FD)
    return;
  if (getLangOpts().CContractsEmitCProver)
    printCProverContracts(FD, Context);
  if (getLangOpts().CContractsEmitCProverUnit)
    if (const ContractSpecifier *CS = FD->getContracts())
      recordCProverRewrites(*CS, Context, CProverUnitRewrites);
}

void Sema::EmitCProverLoopContracts(const Decl *D) {
  if (!D)
    return;
  const auto *FD = dyn_cast<FunctionDecl>(D);
  if (!FD || !FD->hasBody())
    return;
  if (getLangOpts().CContractsEmitCProver)
    printCProverLoopContracts(FD->getBody(), FD, Context);
  if (getLangOpts().CContractsEmitCProverUnit)
    forEachLoopContract(
        FD->getBody(), Context, [&](const Stmt *, const ContractSpecifier &CS) {
          recordCProverRewrites(CS, Context, CProverUnitRewrites);
        });
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

AssignsTarget Sema::ActOnContractAssignsTarget(Expr *Target, Expr *Lower,
                                               Expr *Upper) {
  AssignsTarget Failed;
  if (!Target)
    return Failed;

  // A range names elements of the thing Base points at, so Base must be a
  // pointer or an array; `n[0 : 4]` on an int names nothing.
  if (Upper) {
    QualType T = Target->getType();
    if (!T->isPointerType() && !T->isArrayType()) {
      Diag(Target->getExprLoc(), diag::err_contract_assigns_slice_not_buffer)
          << T << Target->getSourceRange();
      return Failed;
    }
    for (Expr *B : {Lower, Upper}) {
      if (!B)
        continue;
      if (!B->getType()->isIntegerType()) {
        Diag(B->getExprLoc(), diag::err_contract_assigns_bound_not_integer)
            << B->getType() << B->getSourceRange();
        return Failed;
      }
      if (B->HasSideEffects(Context)) {
        Diag(B->getExprLoc(), diag::err_contract_predicate_not_pure)
            << B->getSourceRange();
        return Failed;
      }
    }
    return AssignsTarget{Target, Lower, Upper};
  }

  // A plain frame target names a location, so unlike a predicate it is not
  // converted to bool and keeps its own type. What it must be is an lvalue:
  // `assigns (n)` on a parameter, `assigns (*p)`, `assigns (buf[i])`,
  // `assigns (s->field)`. A value like `assigns (n + 1)` names nothing that
  // could be written to.
  if (!Target->isLValue()) {
    Diag(Target->getExprLoc(), diag::err_contract_assigns_not_lvalue)
        << Target->getSourceRange();
    return Failed;
  }

  // Evaluating a frame target must not change the state it is describing.
  if (Target->HasSideEffects(Context)) {
    Diag(Target->getExprLoc(), diag::err_contract_predicate_not_pure)
        << Target->getSourceRange();
    return Failed;
  }

  return AssignsTarget{Target, nullptr, nullptr};
}

void Sema::ActOnContractAssignsClause(ContractClause &Clause,
                                      ArrayRef<AssignsTarget> Targets) {
  // `assigns ()` is the empty frame: the function modifies nothing. That is a
  // real specification rather than an error, so it still gets a non-null array,
  // which is what lets isInvalid() tell it apart from a parse failure.
  AssignsTarget *Stored = new (Context) AssignsTarget[Targets.size() + 1];
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
