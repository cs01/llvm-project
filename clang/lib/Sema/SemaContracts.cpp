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
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Sema.h"
#include "llvm/Support/raw_ostream.h"
#include <cstring>

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
    if (const FunctionDecl *Callee = findImpureCallee(Cond.get())) {
      // Anyone arriving from CBMC writes __CPROVER_r_ok before they write
      // 'readable', and the purity error is a confusing way to be told so:
      // the predicate has no side effects, the name is simply not an
      // intrinsic. Say that instead, and name the spelling that works.
      StringRef CalleeName = Callee->getName();
      const ContractIntrinsicInfo *Equivalent = nullptr;
      for (const ContractIntrinsicInfo &C : ContractIntrinsicTable)
        if (CalleeName == C.CProver)
          Equivalent = &C;

      if (Equivalent)
        Diag(Callee->getLocation(), diag::note_contract_use_intrinsic)
            << Equivalent->Name << Equivalent->CProver;
      else if (CalleeName.starts_with("__CPROVER"))
        Diag(Callee->getLocation(), diag::note_contract_intrinsics_available);
      else
        Diag(Callee->getLocation(), diag::note_contract_predicate_pure_call)
            << Callee;
    }
    return ExprError();
  }

  return Cond;
}


FunctionDecl *Sema::LookupContractIntrinsic(const IdentifierInfo &II,
                                            SourceLocation Loc) {
  const ContractIntrinsicInfo *Info = findContractIntrinsic(II.getName());
  if (!Info)
    return nullptr;

  auto It = ContractIntrinsics.find(&II);
  if (It != ContractIntrinsics.end())
    return It->second;

  QualType ConstVoidPtr = Context.getPointerType(Context.VoidTy.withConst());
  SmallVector<QualType, 2> ParamTys(Info->NumPtrArgs, ConstVoidPtr);
  if (Info->HasSizeArg)
    ParamTys.push_back(Context.getSizeType());

  // 'pointer_offset' answers with a signed distance; the rest are predicates.
  QualType RetTy = II.getName() == "pointer_offset"
                       ? Context.getPointerDiffType()
                       : Context.IntTy;

  FunctionProtoType::ExtProtoInfo EPI;
  QualType FnTy = Context.getFunctionType(RetTy, ParamTys, EPI);

  DeclContext *DC = Context.getTranslationUnitDecl();
  FunctionDecl *FD = FunctionDecl::Create(
      Context, DC, Loc, Loc, DeclarationName(&II), FnTy,
      Context.getTrivialTypeSourceInfo(FnTy), SC_Extern,
      /*UsesFPIntrin=*/false, /*isInlineSpecified=*/false,
      /*hasWrittenPrototype=*/true, ConstexprSpecKind::Unspecified);
  FD->setImplicit();

  SmallVector<ParmVarDecl *, 3> Params;
  for (QualType T : ParamTys)
    Params.push_back(ParmVarDecl::Create(Context, FD, Loc, Loc, /*Id=*/nullptr,
                                         T, /*TInfo=*/nullptr, SC_None,
                                         /*DefArg=*/nullptr));
  FD->setParams(Params);

  // Marks it usable in a predicate: the purity check below reads exactly this
  // attribute, so the intrinsics go through the same gate as a user's own
  // spec-safe function rather than around it.
  FD->addAttr(ConstAttr::CreateImplicit(Context, Loc));

  ContractIntrinsics[&II] = FD;
  return FD;
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

namespace {
/// Prints contract nodes the way CBMC spells them, instead of patching the
/// printed text afterwards.
///
/// Substitution on the printed string cannot tell `readable(p, n)` resolved to
/// the intrinsic from one that resolved to a function the project declared
/// itself: both print the same characters. It rewrote either, so a codebase
/// with its own `readable` got a proof about CBMC's builtin rather than about
/// its function, and nothing said so. Deciding from the resolved callee is the
/// only way to get that right.
class CProverPrinter : public PrinterHelper {
public:
  bool handledStmt(Stmt *S, raw_ostream &OS) override {
    // `forall (i : lo, hi) P` becomes CBMC's quantifier. The guard is emitted
    // as an implication rather than a conjunction: __CPROVER_forall ranges over
    // every value of the bound variable, so a conjunction would claim P for
    // indices outside the range and make the clause unprovable.
    if (auto *FA = dyn_cast<ContractForallExpr>(S)) {
      std::string Ty = FA->getVar()->getType().getCanonicalType().getAsString(
          Policy);
      StringRef Name = FA->getVar()->getName();

      std::string Lo;
      llvm::raw_string_ostream LoS(Lo);
      FA->getLower()->printPretty(LoS, this, Policy);

      OS << "__CPROVER_forall { " << Ty << " " << Name << "; (";
      // A zero lower bound is implied by the type, and a redundant conjunct is
      // not free: CBMC carries it into the formula.
      if (Lo != "0")
        OS << Name << " >= " << Lo << " && ";
      OS << Name << " < ";
      FA->getUpper()->printPretty(OS, this, Policy);
      OS << ") ==> (";
      FA->getPredicate()->printPretty(OS, this, Policy);
      OS << ") }";
      return true;
    }

    auto *CE = dyn_cast<CallExpr>(S);
    if (!CE)
      return false;
    const FunctionDecl *Callee = CE->getDirectCallee();
    // Only the compiler's own implicit declarations are intrinsics. A user's
    // function of the same name is a different decl and prints as itself.
    if (!Callee || !Callee->isImplicit())
      return false;
    const ContractIntrinsicInfo *Info =
        findContractIntrinsic(Callee->getName());
    if (!Info)
      return false;

    OS << Info->CProver << "(";
    for (unsigned I = 0, N = CE->getNumArgs(); I != N; ++I) {
      if (I)
        OS << ", ";
      CE->getArg(I)->printPretty(OS, this, Policy);
    }
    OS << ")";
    return true;
  }

  PrintingPolicy Policy{LangOptions()};
};
} // namespace

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
    CProverPrinter Helper;
    Helper.Policy = Ctx.getPrintingPolicy();
    llvm::raw_string_ostream OS(Text);
    // The helper renders the intrinsics as CBMC's builtins on the way out, so
    // the decision is made from the callee that was actually resolved.
    P->printPretty(OS, &Helper, Ctx.getPrintingPolicy());
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

/// Records the edits that turn a contracted `do` loop into the `while (1)` form
/// goto-instrument will accept.
///
///   do  CLAUSES { B } while (C);  =>  while (1) CLAUSES { B if (!(C)) break; }
///   ;
///
/// Behaviour is identical -- the body still runs before the test -- and the
/// clauses stay where CBMC wants them, between the header and the body. The
/// original `;` is left as an empty statement rather than hunted down.
///
/// This is the compiler doing the transformation instead of asking the author
/// to restructure shipping code: zstd's hot loops are `do`/`while` by
/// convention, and proofs/zstd/UNBOUNDED.md records the hand rewrite as the
/// first obstacle hit when annotating one.
static void recordDoWhileRewrite(
    const DoStmt *DS, const ASTContext &Ctx,
    SmallVectorImpl<std::pair<SourceRange, std::string>> &Out) {
  const auto *Body = dyn_cast<CompoundStmt>(DS->getBody());
  if (!Body)
    return; // Diagnosed by the caller: there is no brace to hang the test on.

  const SourceManager &SM = Ctx.getSourceManager();
  StringRef Cond = Lexer::getSourceText(
      CharSourceRange::getTokenRange(DS->getCond()->getSourceRange()), SM,
      Ctx.getLangOpts());

  // `do` -> `while (1)`. A single token, so begin and end are the same.
  Out.emplace_back(SourceRange(DS->getBeginLoc(), DS->getBeginLoc()),
                   "while (1)");

  // The body's closing brace carries the exit test.
  Out.emplace_back(SourceRange(Body->getRBracLoc(), Body->getRBracLoc()),
                   ("if (!(" + Cond + ")) break; }").str());

  // The trailing `while (C)` has nothing left to say.
  Out.emplace_back(SourceRange(DS->getWhileLoc(), DS->getRParenLoc()), "");
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
static void
recordCProverRewrites(const ContractSpecifier &CS, const ASTContext &Ctx,
                      SmallVectorImpl<std::pair<SourceRange, std::string>> &Out,
                      unsigned &NumInvalid) {
  for (const ContractClause &Clause : CS) {
    std::string Text = formatCProverClause(Clause, Ctx);
    if (!Text.empty())
      Out.emplace_back(Clause.getSourceRange(), std::move(Text));
    else if (Clause.isInvalid())
      ++NumInvalid;
  }
}

/// Emits a CBMC entry point for \p FD, built from its preconditions.
///
/// This is what makes a contract usable on a function with loops.
/// `goto-instrument --enforce-contract` refuses unless *every* loop-shaped
/// construct in the function already carries a contract -- including
/// `do { } while (0)` macros and loops in branches the precondition excludes --
/// which measured out at eleven loops for zlib's `inflate_table`, or roughly
/// thirty-three clauses before a seven-clause contract could be checked at all.
/// See proofs/CHECKER-GAP-loop-requirement.md.
///
/// A harness needs none of that: CBMC unwinds normally. And the harness is the
/// one thing an author should never be writing by hand, because a hand-written
/// `__CPROVER_assume` is an assumption nobody reviews. Generating it from the
/// contract keeps the reviewable artifact in the source.
///
///   void f(char *p, size_t n) pre (fresh(p, n)) pre (n > 0 && n < 64);
///
/// becomes
///
///   void __contract_harness_f(void) {
///     size_t n; char *p;
///     __CPROVER_assume(n > 0 && n < 64);
///     p = __CPROVER_allocate(n, 0);
///     f(p, n);
///   }
///
/// Uninitialised locals are nondeterministic in CBMC, so a parameter no clause
/// mentions is simply unconstrained -- which is the honest default: the
/// contract said nothing about it.
static void emitContractHarness(const FunctionDecl *FD, Sema &S,
                                llvm::raw_ostream &OS) {
  const ASTContext &Ctx = S.Context;
  const ContractSpecifier *CS = FD->getContracts();
  if (!CS)
    return;

  auto Print = [&](const Expr *E) {
    std::string T;
    llvm::raw_string_ostream SS(T);
    CProverPrinter Helper;
    Helper.Policy = Ctx.getPrintingPolicy();
    E->printPretty(SS, &Helper, Ctx.getPrintingPolicy());
    return T;
  };

  // A `fresh(L, N)` precondition allocates rather than assumes: it is the
  // clause that says how big the object is, and CBMC cannot conjure that from
  // an assumption over an unconstrained pointer. L is any lvalue, not just a
  // parameter, so a caller-allocated buffer reached through an out-parameter --
  // zlib's `code **table` -- is expressible as `fresh(*table, n)`.
  struct Step {
    bool IsAlloc;
    std::string Text; // predicate, or allocation target
    std::string Size; // allocation only
  };
  SmallVector<Step, 8> Steps;

  // Clauses are emitted in source order, so an ordering mistake is silent
  // otherwise: a size read before the clause bounding it makes the entry point
  // allocate an unbounded object, and every property downstream then holds
  // vacuously or fails for the wrong reason. Track which parameters an earlier
  // *assumption* has constrained -- allocating a pointer says nothing about the
  // value stored through it, so `fresh(bits, ...)` does not constrain `*bits`.
  llvm::SmallPtrSet<const ParmVarDecl *, 8> Constrained;
  auto ParmsIn = [](const Expr *E,
                    llvm::SmallVectorImpl<const ParmVarDecl *> &Out) {
    struct V : RecursiveASTVisitor<V> {
      llvm::SmallVectorImpl<const ParmVarDecl *> &Out;
      V(llvm::SmallVectorImpl<const ParmVarDecl *> &Out) : Out(Out) {}
      bool VisitDeclRefExpr(DeclRefExpr *DRE) {
        if (const auto *P = dyn_cast<ParmVarDecl>(DRE->getDecl()))
          Out.push_back(P);
        return true;
      }
    } Vis(Out);
    Vis.TraverseStmt(const_cast<Expr *>(E));
  };

  for (const ContractClause &C : *CS) {
    if (C.getKind() != ContractClause::CK_Pre || C.isInvalid())
      continue;
    const Expr *Pred = C.getPredicate();
    const auto *Call = dyn_cast<CallExpr>(Pred->IgnoreParenImpCasts());
    const FunctionDecl *Callee = Call ? Call->getDirectCallee() : nullptr;
    const Expr *Target =
        (Callee && Callee->isImplicit() && Callee->getName() == "fresh" &&
         Call->getNumArgs() == 2)
            ? Call->getArg(0)->IgnoreParenImpCasts()
            : nullptr;
    // Only an lvalue can be allocated into. `fresh(p + 1, n)` is a legitimate
    // thing to assume and a meaningless thing to assign to, so it stays an
    // assumption.
    if (Target && Target->isLValue()) {
      const Expr *Size = Call->getArg(1);
      llvm::SmallVector<const ParmVarDecl *, 4> Used;
      ParmsIn(Size, Used);
      for (const ParmVarDecl *P : Used) {
        if (Constrained.count(P))
          continue;
        S.Diag(Size->getExprLoc(), diag::warn_contract_harness_unbounded_alloc)
            << Print(Target) << P;
        S.Diag(Size->getExprLoc(), diag::note_contract_harness_order) << P;
        break;
      }
      Steps.push_back({true, Print(Target), Print(Size)});
      continue;
    }
    llvm::SmallVector<const ParmVarDecl *, 4> Used;
    ParmsIn(Pred, Used);
    Constrained.insert(Used.begin(), Used.end());
    Steps.push_back({false, Print(Pred), ""});
  }

  OS << "\n/* entry point generated from " << FD->getNameAsString()
     << "'s contract; do not write one by hand */\n";
  OS << "void __contract_harness_" << FD->getNameAsString() << "(void) {\n";

  // A parameter no clause mentions is left uninitialised, which is
  // nondeterministic in CBMC: the contract said nothing about it, so neither
  // does the entry point.
  for (const ParmVarDecl *P : FD->parameters()) {
    if (P->getName().empty())
      continue;
    OS << "  " << P->getType().getAsString(Ctx.getPrintingPolicy()) << " "
       << P->getName() << ";\n";
  }
  for (const Step &St : Steps) {
    if (St.IsAlloc)
      OS << "  " << St.Text << " = __CPROVER_allocate(" << St.Size << ", 0);\n";
    else
      OS << "  __CPROVER_assume(" << St.Text << ");\n";
  }

  OS << "  " << FD->getNameAsString() << "(";
  bool First = true;
  for (const ParmVarDecl *P : FD->parameters()) {
    if (!First)
      OS << ", ";
    First = false;
    OS << (P->getName().empty() ? "0" : P->getName());
  }
  OS << ");\n}\n";
}

void Sema::EmitCProverUnit() {
  if (!getLangOpts().CContractsEmitCProverUnit)
    return;

  // A clause that did not type-check has no rewrite, so its original text
  // would survive into the output next to the clauses that were rewritten.
  // That file is neither this grammar nor CBMC's, compiles as neither, and
  // says nothing about which half is missing. Refuse instead: the errors are
  // already on stderr, and half a translation unit is worse than none.
  SourceManager &SM = getSourceManager();
  if (NumInvalidContractClauses) {
    Diag(SM.getLocForStartOfFile(SM.getMainFileID()),
         diag::err_contract_cprover_unit_invalid_clause)
        << NumInvalidContractClauses;
    return;
  }

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

  // The harness goes last: it calls the function, so the definition has to
  // precede it in the emitted unit.
  if (getLangOpts().CContractsEmitHarness)
    for (Decl *D : Context.getTranslationUnitDecl()->decls())
      if (const auto *FD = dyn_cast<FunctionDecl>(D))
        if (FD->getContracts() && FD->doesThisDeclarationHaveABody())
          emitContractHarness(FD, *this, llvm::outs());
}

void Sema::ActOnFunctionContracts(Declarator &D, FunctionDecl *FD) {
  if (!D.hasContractClauses() || !FD)
    return;

  // Restating contracts on a redeclaration would mean comparing two predicates
  // written against different ParmVarDecls for equivalence. That is not
  // implemented, and silently keeping one of the two would make which
  // declaration a caller happened to see change what gets checked. Reject it
  // instead, which also lets getContractDecl assume at most one carrier.
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
      recordCProverRewrites(*CS, Context, CProverUnitRewrites,
                            NumInvalidContractClauses);
}

/// The first loop under \p S that carries no loop contract, or null.
///
/// Contract checking rewrites a function into straight-line code before it can
/// check a frame, so a single un-annotated loop anywhere in the body -- or in
/// anything inlined into it -- makes the function's own contract unusable.
static const Stmt *findUncontractedLoop(const Stmt *S, const ASTContext &Ctx) {
  if (!S)
    return nullptr;
  if (isa<WhileStmt>(S) || isa<ForStmt>(S) || isa<DoStmt>(S))
    if (!Ctx.getLoopContracts(S))
      return S;
  for (const Stmt *Child : S->children())
    if (const Stmt *Found = findUncontractedLoop(Child, Ctx))
      return Found;
  return nullptr;
}

/// Whether \p E ultimately addresses storage reached through a pointer
/// parameter, so that writing to it escapes the function.
static bool rootsAtPointerParam(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  // Both `s->f` and `s.f` recurse on the base: a `.` on a by-value parameter
  // bottoms out at a non-pointer DeclRefExpr below and answers false anyway.
  if (const auto *ME = dyn_cast<MemberExpr>(E))
    return rootsAtPointerParam(ME->getBase());
  if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E))
    return rootsAtPointerParam(ASE->getBase());
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    if (UO->getOpcode() == UO_Deref)
      return rootsAtPointerParam(UO->getSubExpr());
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (const auto *PVD = dyn_cast<ParmVarDecl>(DRE->getDecl()))
      return PVD->getType()->isPointerType();
  return false;
}

/// The first write through a pointer parameter under \p S, or null.
static const Expr *findWriteThroughParam(const Stmt *S) {
  if (!S)
    return nullptr;
  if (const auto *BO = dyn_cast<BinaryOperator>(S))
    if (BO->isAssignmentOp() && rootsAtPointerParam(BO->getLHS()))
      return BO;
  if (const auto *UO = dyn_cast<UnaryOperator>(S))
    if (UO->isIncrementDecrementOp() && rootsAtPointerParam(UO->getSubExpr()))
      return UO;
  for (const Stmt *Child : S->children())
    if (const Expr *Found = findWriteThroughParam(Child))
      return Found;
  return nullptr;
}

void Sema::DiagnoseContractVerifiability(const FunctionDecl *FD) {
  if (!FD || !FD->hasContracts() || !FD->hasBody())
    return;
  const ContractSpecifier *CS = FD->getContracts();
  const Stmt *Body = FD->getBody();

  // An always_inline callee is pasted into its caller before contracts are
  // applied, so neither its frame nor its loops' invariants survive. Accepting
  // the clause and having it do nothing is the one outcome that misleads.
  if (const auto *AI = FD->getAttr<AlwaysInlineAttr>()) {
    Diag(CS->clauses().front().getKeywordLoc(),
         diag::warn_contract_on_always_inline)
        << FD;
    Diag(AI->getLocation(), diag::note_contract_always_inline_here);
  }

  // A contract with no frame is checked against an empty one, so every write
  // the function makes is a violation -- five diagnostics about the body when
  // the fault is a missing clause in the header.
  bool HasAssigns = false;
  for (const ContractClause &C : CS->clauses())
    if (C.getKind() == ContractClause::CK_Assigns)
      HasAssigns = true;
  if (!HasAssigns)
    if (const Expr *W = findWriteThroughParam(Body)) {
      Diag(CS->clauses().front().getKeywordLoc(),
           diag::warn_contract_missing_assigns)
          << FD;
      Diag(W->getExprLoc(), diag::note_contract_missing_assigns_write)
          << W->getSourceRange();
    }

  if (const Stmt *L = findUncontractedLoop(Body, Context)) {
    Diag(CS->clauses().front().getKeywordLoc(),
         diag::warn_contract_fn_uncontracted_loop)
        << FD;
    Diag(L->getBeginLoc(), diag::note_contract_uncontracted_loop_here);
  }
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
        FD->getBody(), Context,
        [&](const Stmt *L, const ContractSpecifier &CS) {
          recordCProverRewrites(CS, Context, CProverUnitRewrites,
                                NumInvalidContractClauses);
          // A 'do' loop also needs restructuring, not just clause
          // substitution, because goto-instrument refuses loop contracts on
          // one. The author keeps their loop; the emitted unit gets the shape
          // the prover accepts.
          if (const auto *DS = dyn_cast<DoStmt>(L)) {
            if (isa<CompoundStmt>(DS->getBody()))
              recordDoWhileRewrite(DS, Context, CProverUnitRewrites);
            else
              Diag(DS->getBeginLoc(), diag::warn_contract_do_needs_braces);
          }
        });
}

VarDecl *Sema::ActOnContractForallVar(Scope *S, IdentifierInfo *II,
                                      SourceLocation Loc) {
  // size_t, because every use indexes a buffer or a table. A signed variable
  // would need a `>= 0` clause the author did not write.
  QualType T = Context.getSizeType();
  VarDecl *Var = VarDecl::Create(Context, CurContext, Loc, Loc, II, T,
                                 Context.getTrivialTypeSourceInfo(T), SC_None);
  Var->setImplicit();
  PushOnScopeChains(Var, S, /*AddToContext=*/false);
  return Var;
}

ExprResult Sema::BuildContractForallExpr(SourceLocation ForallLoc,
                                         SourceLocation LParenLoc,
                                         SourceLocation RParenLoc,
                                         VarDecl *Var, Expr *Lower,
                                         Expr *Upper, Expr *Pred) {
  if (!Lower || !Upper || !Pred || !Var)
    return ExprError();

  // The bounds index a range, so they have to be integers; saying so here
  // beats a confusing failure inside the emitted __CPROVER_forall.
  for (Expr *Bound : {Lower, Upper}) {
    ExprResult R = DefaultLvalueConversion(Bound);
    if (R.isInvalid())
      return ExprError();
    if (!R.get()->getType()->isIntegerType()) {
      Diag(Bound->getExprLoc(), diag::err_contract_forall_bound_not_integer)
          << R.get()->getType() << Bound->getSourceRange();
      return ExprError();
    }
    (Bound == Lower ? Lower : Upper) = R.get();
  }

  // The body is a condition, like every other contract predicate.
  ExprResult Cond = CheckBooleanCondition(ForallLoc, Pred);
  if (Cond.isInvalid())
    return ExprError();

  return new (Context) ContractForallExpr(Context, ForallLoc, LParenLoc,
                                          RParenLoc, Var, Lower, Upper,
                                          Cond.get());
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

/// Returns the first parameter whose *value* is read anywhere in \p E outside
/// an `old`, or null.
///
/// Descent stops at a ContractOldExpr: naming a parameter there is exactly the
/// supported way to do it, so those references are not the ambiguous ones.
///
/// It also stops at a dereference. `post (*op - *ip >= 8)` -- which is
/// `ZSTD_overlapCopy8`'s own documented postcondition -- reads memory, not the
/// parameter, and memory is shared with the caller: the pointer is the one that
/// was passed in and the bytes are the ones the body left behind. There is
/// nothing for a reader to be confused about and nothing for `old` to add, so
/// requiring it there only made real contracts unwritable. The ambiguity the
/// rule exists for is a *value* read, `post (n > 0)`, where a body that did
/// `n -= k` leaves the reader unable to tell which `n` was meant.
static const DeclRefExpr *findBareParameterRef(const Stmt *E) {
  if (isa<ContractOldExpr>(E))
    return nullptr;

  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (isa<ParmVarDecl>(DRE->getDecl()))
      return DRE;

  // The pointer a load goes through addresses memory rather than reporting its
  // own value, so it is exempt -- but only that pointer. `post (buf[i] == 0)`
  // still has to diagnose `i`, which is an ordinary value read and is exactly
  // as ambiguous as `post (i > 0)` when the body writes to it.
  const Expr *LoadedThrough = nullptr;
  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Deref)
      LoadedThrough = UO->getSubExpr();
  } else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(E)) {
    LoadedThrough = ASE->getBase();
  } else if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (ME->isArrow())
      LoadedThrough = ME->getBase();
  }

  const DeclRefExpr *Exempt = nullptr;
  if (LoadedThrough) {
    const Expr *Base = LoadedThrough->IgnoreParenImpCasts();
    // Only a bare pointer parameter. `*(p + i)` reads i's value to choose the
    // address, so that one is still reported.
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base))
      if (isa<ParmVarDecl>(DRE->getDecl()) && DRE->getType()->isPointerType())
        Exempt = DRE;
  }

  for (const Stmt *Child : E->children())
    if (Child)
      if (const DeclRefExpr *Found = findBareParameterRef(Child))
        if (Found != Exempt)
          return Found;

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
