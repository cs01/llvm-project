//===- ContractChecking.cpp - Compile-time contract checking --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/ContractChecking.h"
#include "clang/AST/ContractSpecifier.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/PostOrderIterator.h"

using namespace clang;
using namespace clang::contracts;

namespace {

/// What the pass knows about one value.
///
/// Deliberately tiny. The pass reports only violations it can demonstrate, so
/// the lattice needs to represent "definitely this" and "no idea", and nothing
/// in between earns its complexity yet.
struct AbstractValue {
  enum Kind { Unknown, Int, Null, NonNull };
  Kind K = Unknown;
  llvm::APSInt IntVal;

  static AbstractValue unknown() { return AbstractValue(); }
  static AbstractValue makeInt(llvm::APSInt V) {
    AbstractValue A;
    A.K = Int;
    A.IntVal = std::move(V);
    return A;
  }
  static AbstractValue makeBool(bool B) {
    return makeInt(llvm::APSInt::get(B ? 1 : 0));
  }
  static AbstractValue null() {
    AbstractValue A;
    A.K = Null;
    return A;
  }
  static AbstractValue nonNull() {
    AbstractValue A;
    A.K = NonNull;
    return A;
  }

  bool isKnownFalse() const { return K == Int && IntVal == 0; }

  bool operator==(const AbstractValue &O) const {
    if (K != O.K)
      return false;
    if (K == Int)
      return llvm::APSInt::isSameValue(IntVal, O.IntVal);
    return true;
  }
};

using State = llvm::DenseMap<const VarDecl *, AbstractValue>;

/// Collects every variable whose address is taken.
///
/// Such a variable can be written through a pointer the pass cannot follow, so
/// it is never tracked. This is the cheap defence against the out-parameter
/// false positive (`T *p = 0; f(&p); p->x;`) that dominates this class of
/// analysis in practice.
class AddressTakenCollector
    : public RecursiveASTVisitor<AddressTakenCollector> {
public:
  llvm::DenseSet<const VarDecl *> AddressTaken;

  bool VisitUnaryOperator(UnaryOperator *UO) {
    if (UO->getOpcode() != UO_AddrOf)
      return true;
    if (const auto *DRE =
            dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenCasts()))
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        AddressTaken.insert(VD);
    return true;
  }
};

/// Evaluates an expression against a state, optionally substituting a callee's
/// parameters with the abstract values of the arguments at a call site.
class Evaluator {
  const ASTContext &Ctx;
  const State &S;
  const llvm::DenseMap<const ParmVarDecl *, AbstractValue> *Subst;

public:
  Evaluator(const ASTContext &Ctx, const State &S,
            const llvm::DenseMap<const ParmVarDecl *, AbstractValue> *Subst)
      : Ctx(Ctx), S(S), Subst(Subst) {}

  AbstractValue eval(const Expr *E) const {
    if (!E)
      return AbstractValue::unknown();

    if (E->getType()->isPointerType() &&
        E->isNullPointerConstant(const_cast<ASTContext &>(Ctx),
                                 Expr::NPC_ValueDependentIsNotNull))
      return AbstractValue::null();

    const Expr *X = E->IgnoreParenImpCasts();

    if (const auto *IL = dyn_cast<IntegerLiteral>(X))
      return AbstractValue::makeInt(
          llvm::APSInt(IL->getValue(), X->getType()->isUnsignedIntegerType()));

    if (isa<StringLiteral>(X))
      return AbstractValue::nonNull();

    if (const auto *Old = dyn_cast<ContractOldExpr>(X))
      return eval(Old->getSubExpr());

    if (const auto *UO = dyn_cast<UnaryOperator>(X)) {
      if (UO->getOpcode() == UO_AddrOf)
        return AbstractValue::nonNull();
      if (UO->getOpcode() == UO_LNot) {
        AbstractValue V = eval(UO->getSubExpr());
        if (V.K == AbstractValue::Int)
          return AbstractValue::makeBool(V.IntVal == 0);
        if (V.K == AbstractValue::Null)
          return AbstractValue::makeBool(true);
        if (V.K == AbstractValue::NonNull)
          return AbstractValue::makeBool(false);
      }
      return AbstractValue::unknown();
    }

    if (const auto *DRE = dyn_cast<DeclRefExpr>(X)) {
      const ValueDecl *D = DRE->getDecl();
      if (Subst)
        if (const auto *PVD = dyn_cast<ParmVarDecl>(D)) {
          auto It = Subst->find(PVD);
          if (It != Subst->end())
            return It->second;
          return AbstractValue::unknown();
        }
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        auto It = S.find(VD);
        if (It != S.end())
          return It->second;
      }
      return AbstractValue::unknown();
    }

    if (const auto *BO = dyn_cast<BinaryOperator>(X))
      return evalBinary(BO);

    return AbstractValue::unknown();
  }

private:
  AbstractValue evalBinary(const BinaryOperator *BO) const {
    AbstractValue L = eval(BO->getLHS());

    // Short-circuit before evaluating the right operand, so that
    // `p != 0 && p->n > 0` does not need the right side to be decidable.
    if (BO->getOpcode() == BO_LAnd) {
      if (L.isKnownFalse())
        return AbstractValue::makeBool(false);
      AbstractValue R = eval(BO->getRHS());
      if (R.isKnownFalse())
        return AbstractValue::makeBool(false);
      if (isKnownTrue(L) && isKnownTrue(R))
        return AbstractValue::makeBool(true);
      return AbstractValue::unknown();
    }
    if (BO->getOpcode() == BO_LOr) {
      if (isKnownTrue(L))
        return AbstractValue::makeBool(true);
      AbstractValue R = eval(BO->getRHS());
      if (isKnownTrue(R))
        return AbstractValue::makeBool(true);
      if (L.isKnownFalse() && R.isKnownFalse())
        return AbstractValue::makeBool(false);
      return AbstractValue::unknown();
    }

    if (!BO->isComparisonOp())
      return AbstractValue::unknown();

    AbstractValue R = eval(BO->getRHS());

    // Pointer comparisons against a known null or non-null.
    bool LIsPtr = L.K == AbstractValue::Null || L.K == AbstractValue::NonNull;
    bool RIsPtr = R.K == AbstractValue::Null || R.K == AbstractValue::NonNull;
    if ((LIsPtr && RIsPtr) ||
        (LIsPtr && R.K == AbstractValue::Int && R.IntVal == 0) ||
        (RIsPtr && L.K == AbstractValue::Int && L.IntVal == 0)) {
      auto AsNull = [](const AbstractValue &V) {
        return V.K == AbstractValue::Null ||
               (V.K == AbstractValue::Int && V.IntVal == 0);
      };
      bool BothNull = AsNull(L) && AsNull(R);
      bool OneNullOneNot = (AsNull(L) && R.K == AbstractValue::NonNull) ||
                           (AsNull(R) && L.K == AbstractValue::NonNull);
      if (BO->getOpcode() == BO_EQ) {
        if (BothNull)
          return AbstractValue::makeBool(true);
        if (OneNullOneNot)
          return AbstractValue::makeBool(false);
      }
      if (BO->getOpcode() == BO_NE) {
        if (BothNull)
          return AbstractValue::makeBool(false);
        if (OneNullOneNot)
          return AbstractValue::makeBool(true);
      }
      return AbstractValue::unknown();
    }

    if (L.K != AbstractValue::Int || R.K != AbstractValue::Int)
      return AbstractValue::unknown();

    int Cmp = llvm::APSInt::compareValues(L.IntVal, R.IntVal);
    switch (BO->getOpcode()) {
    case BO_EQ:
      return AbstractValue::makeBool(Cmp == 0);
    case BO_NE:
      return AbstractValue::makeBool(Cmp != 0);
    case BO_LT:
      return AbstractValue::makeBool(Cmp < 0);
    case BO_LE:
      return AbstractValue::makeBool(Cmp <= 0);
    case BO_GT:
      return AbstractValue::makeBool(Cmp > 0);
    case BO_GE:
      return AbstractValue::makeBool(Cmp >= 0);
    default:
      return AbstractValue::unknown();
    }
  }

  static bool isKnownTrue(const AbstractValue &V) {
    if (V.K == AbstractValue::Int)
      return V.IntVal != 0;
    return V.K == AbstractValue::NonNull;
  }
};

class ContractChecker {
  AnalysisDeclContext &AC;
  const ASTContext &Ctx;
  ContractViolationReporter &Reporter;
  llvm::DenseSet<const VarDecl *> AddressTaken;

public:
  ContractChecker(AnalysisDeclContext &AC, const ASTContext &Ctx,
                  ContractViolationReporter &Reporter)
      : AC(AC), Ctx(Ctx), Reporter(Reporter) {}

  void run();

private:
  bool tracked(const VarDecl *VD) const {
    return VD && VD->isLocalVarDeclOrParm() &&
           !VD->getType()->isReferenceType() && !AddressTaken.count(VD);
  }

  /// Narrows \p S using \p Cond known to have evaluated to \p TrueBranch.
  void refine(State &S, const Expr *Cond, bool TrueBranch);

  void transfer(State &S, const Stmt *St);
  void checkCall(const State &S, const CallExpr *Call);

  /// What a call's postcondition guarantees about its result.
  ///
  /// This is what makes the pass compositional: without it the checker only
  /// ever catches literal arguments, and `p = allocate(n); use(p);` stays
  /// unknown even though the callee promised a non-null result.
  AbstractValue valueFromPost(const Expr *E) const;

  /// Evaluates \p E, falling back to what a called function's postcondition
  /// promises when the expression itself is opaque.
  AbstractValue evalWithPost(const State &S, const Expr *E) const {
    AbstractValue V = Evaluator(Ctx, S, nullptr).eval(E);
    if (V.K != AbstractValue::Unknown)
      return V;
    return valueFromPost(E);
  }
};

void ContractChecker::refine(State &S, const Expr *Cond, bool TrueBranch) {
  if (!Cond)
    return;
  const Expr *X = Cond->IgnoreParenImpCasts();

  if (const auto *UO = dyn_cast<UnaryOperator>(X)) {
    if (UO->getOpcode() == UO_LNot)
      refine(S, UO->getSubExpr(), !TrueBranch);
    return;
  }

  if (const auto *DRE = dyn_cast<DeclRefExpr>(X)) {
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (tracked(VD) && VD->getType()->isPointerType())
      S[VD] = TrueBranch ? AbstractValue::nonNull() : AbstractValue::null();
    return;
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(X)) {
    // Both operands of `&&` hold on the true edge; both operands of `||` fail
    // on the false edge. The other two directions say nothing.
    if ((BO->getOpcode() == BO_LAnd && TrueBranch) ||
        (BO->getOpcode() == BO_LOr && !TrueBranch)) {
      refine(S, BO->getLHS(), TrueBranch);
      refine(S, BO->getRHS(), TrueBranch);
      return;
    }
    if (BO->getOpcode() != BO_EQ && BO->getOpcode() != BO_NE)
      return;

    const Expr *L = BO->getLHS()->IgnoreParenImpCasts();
    const Expr *R = BO->getRHS()->IgnoreParenImpCasts();
    const DeclRefExpr *DRE = dyn_cast<DeclRefExpr>(L);
    const Expr *Other = R;
    if (!DRE) {
      DRE = dyn_cast<DeclRefExpr>(R);
      Other = L;
    }
    if (!DRE)
      return;
    const auto *VD = dyn_cast<VarDecl>(DRE->getDecl());
    if (!tracked(VD) || !VD->getType()->isPointerType())
      return;
    if (!Other->isNullPointerConstant(const_cast<ASTContext &>(Ctx),
                                      Expr::NPC_ValueDependentIsNotNull))
      return;

    bool IsNull = (BO->getOpcode() == BO_EQ) == TrueBranch;
    S[VD] = IsNull ? AbstractValue::null() : AbstractValue::nonNull();
  }
}

void ContractChecker::transfer(State &S, const Stmt *St) {
  if (const auto *DS = dyn_cast<DeclStmt>(St)) {
    for (const Decl *D : DS->decls())
      if (const auto *VD = dyn_cast<VarDecl>(D))
        if (tracked(VD))
          S[VD] = VD->getInit() ? evalWithPost(S, VD->getInit())
                                : AbstractValue::unknown();
    return;
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(St)) {
    if (BO->isAssignmentOp())
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts()))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (tracked(VD))
            S[VD] = BO->getOpcode() == BO_Assign ? evalWithPost(S, BO->getRHS())
                                                 : AbstractValue::unknown();
    return;
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(St)) {
    if (UO->isIncrementDecrementOp())
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(UO->getSubExpr()->IgnoreParenImpCasts()))
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
          if (tracked(VD))
            S[VD] = AbstractValue::unknown();
    return;
  }

  if (const auto *Call = dyn_cast<CallExpr>(St))
    checkCall(S, Call);
}

/// Returns true if \p Pred, read as a postcondition over \p Result, guarantees
/// that the result is non-null.
static bool postImpliesNonNull(const Expr *Pred, const VarDecl *Result) {
  if (!Pred || !Result)
    return false;
  const Expr *X = Pred->IgnoreParenImpCasts();

  if (const auto *BO = dyn_cast<BinaryOperator>(X)) {
    // A conjunction guarantees each of its operands.
    if (BO->getOpcode() == BO_LAnd)
      return postImpliesNonNull(BO->getLHS(), Result) ||
             postImpliesNonNull(BO->getRHS(), Result);

    if (BO->getOpcode() == BO_NE) {
      const Expr *L = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *R = BO->getRHS()->IgnoreParenImpCasts();
      const auto *DRE = dyn_cast<DeclRefExpr>(L);
      const Expr *Other = R;
      if (!DRE) {
        DRE = dyn_cast<DeclRefExpr>(R);
        Other = L;
      }
      if (DRE && DRE->getDecl() == Result &&
          Other->isNullPointerConstant(Result->getASTContext(),
                                       Expr::NPC_ValueDependentIsNotNull))
        return true;
    }
    return false;
  }

  // `post (r: r)` on a pointer says the same thing.
  if (const auto *DRE = dyn_cast<DeclRefExpr>(X))
    return DRE->getDecl() == Result && DRE->getType()->isPointerType();

  return false;
}

AbstractValue ContractChecker::valueFromPost(const Expr *E) const {
  const auto *Call = dyn_cast<CallExpr>(E->IgnoreParenImpCasts());
  if (!Call || !Call->getDirectCallee())
    return AbstractValue::unknown();
  const FunctionDecl *ContractDecl = Call->getDirectCallee()->getContractDecl();
  if (!ContractDecl)
    return AbstractValue::unknown();

  for (const ContractClause &Clause : *ContractDecl->getContracts())
    if (Clause.getKind() == ContractClause::CK_Post &&
        postImpliesNonNull(Clause.getPredicate(), Clause.getResultVar()))
      return AbstractValue::nonNull();

  return AbstractValue::unknown();
}

void ContractChecker::checkCall(const State &S, const CallExpr *Call) {
  const FunctionDecl *Callee = Call->getDirectCallee();
  if (!Callee)
    return;
  const FunctionDecl *ContractDecl = Callee->getContractDecl();
  if (!ContractDecl)
    return;
  const ContractSpecifier *CS = ContractDecl->getContracts();

  // The predicate names the parameters of whichever declaration spelled the
  // contract, which is usually the prototype in a header and not the callee
  // this call resolved to. A prototype and its definition have distinct
  // ParmVarDecls, so the substitution has to be keyed on the declaring one.
  llvm::DenseMap<const ParmVarDecl *, AbstractValue> Subst;
  unsigned N = std::min(Call->getNumArgs(), ContractDecl->getNumParams());
  Evaluator ArgEval(Ctx, S, nullptr);
  for (unsigned I = 0; I != N; ++I)
    Subst[ContractDecl->getParamDecl(I)] = ArgEval.eval(Call->getArg(I));

  Evaluator PredEval(Ctx, S, &Subst);
  for (const ContractClause &Clause : *CS) {
    if (Clause.getKind() != ContractClause::CK_Pre || !Clause.getPredicate())
      continue;
    if (PredEval.eval(Clause.getPredicate()).isKnownFalse())
      Reporter.reportPreconditionViolated(Call, Callee, Clause);
  }
}

void ContractChecker::run() {
  const auto *FD = dyn_cast_or_null<FunctionDecl>(AC.getDecl());
  CFG *Cfg = AC.getCFG();
  if (!FD || !Cfg)
    return;

  AddressTakenCollector Collector;
  Collector.TraverseStmt(const_cast<Stmt *>(AC.getBody()));
  AddressTaken = std::move(Collector.AddressTaken);

  // The function's own preconditions hold on entry, which is what lets a call
  // inside the body be discharged by a guarantee its caller already made.
  State Entry;
  if (const ContractSpecifier *Own = FD->getContractsForCall())
    for (const ContractClause &Clause : *Own)
      if (Clause.getKind() == ContractClause::CK_Pre && Clause.getPredicate())
        refine(Entry, Clause.getPredicate(), /*TrueBranch=*/true);

  llvm::DenseMap<const CFGBlock *, State> BlockEntry;
  BlockEntry[&Cfg->getEntry()] = std::move(Entry);

  // A single reverse-post-order sweep. Back edges are not iterated to a
  // fixpoint: a block is analysed from the predecessors that reach it forwards,
  // which is exactly the path the report would describe, and skipping the
  // fixpoint costs only missed reports, never invented ones.
  llvm::ReversePostOrderTraversal<CFG *> RPO(Cfg);
  for (const CFGBlock *B : RPO) {
    State Cur;
    bool First = true;
    for (const CFGBlock *Pred : B->preds()) {
      if (!Pred)
        continue;
      auto It = BlockEntry.find(Pred);
      if (It == BlockEntry.end())
        continue; // Not reached forwards yet: a back edge.

      State Edge = It->second;
      if (const Stmt *Cond = Pred->getTerminatorCondition()) {
        if (const auto *CondE = dyn_cast<Expr>(Cond)) {
          bool IsTrueEdge = Pred->succ_size() > 0 && *Pred->succ_begin() == B;
          bool IsFalseEdge =
              Pred->succ_size() > 1 && *(Pred->succ_begin() + 1) == B;
          // Only a two-way branch says anything; a switch edge does not.
          if (IsTrueEdge != IsFalseEdge)
            refine(Edge, CondE, IsTrueEdge);
        }
      }

      if (First) {
        Cur = std::move(Edge);
        First = false;
        continue;
      }
      // Merge: keep only what every predecessor agrees on. Disagreement means
      // the pass does not know, and not knowing must never produce a report.
      llvm::SmallVector<const VarDecl *, 8> Disagreed;
      for (const auto &KV : Cur) {
        auto Found = Edge.find(KV.first);
        if (Found == Edge.end() || !(Found->second == KV.second))
          Disagreed.push_back(KV.first);
      }
      for (const VarDecl *VD : Disagreed)
        Cur.erase(VD);
    }

    if (B == &Cfg->getEntry())
      Cur = BlockEntry[B];

    for (const CFGElement &Elem : *B)
      if (std::optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
        transfer(Cur, CS->getStmt());

    BlockEntry[B] = std::move(Cur);
  }
}

} // namespace

void clang::contracts::runContractChecking(
    AnalysisDeclContext &AC, ContractViolationReporter &Reporter) {
  ContractChecker(AC, AC.getASTContext(), Reporter).run();
}
