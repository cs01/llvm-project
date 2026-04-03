//===- FlowNullability.cpp - Flow-sensitive null dereference checking -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a CFG-based forward dataflow analysis that detects
// dereferences of nullable pointers, tracking nullability narrowing through
// control flow (null checks, early returns, assertions, etc.).
//
//===----------------------------------------------------------------------===//

#include "clang/Analysis/Analyses/FlowNullability.h"
#include "clang/AST/Attr.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/AST/OperationKinds.h"
#include "clang/AST/Stmt.h"
#include "clang/AST/Type.h"
#include "clang/Analysis/AnalysisDeclContext.h"
#include "clang/Analysis/CFG.h"
#include "clang/Analysis/FlowSensitive/DataflowWorklist.h"
#include "clang/Basic/Builtins.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>
#include <utility>

#define DEBUG_TYPE "flow-nullability"

STATISTIC(NumFunctionsAnalyzed, "Number of functions analyzed");
STATISTIC(NumBlocksProcessed, "Number of CFG blocks processed");
STATISTIC(NumFixpointIterations, "Number of fixpoint iterations");
STATISTIC(NumDereferenceWarnings, "Number of nullable dereference warnings");
STATISTIC(NumArithmeticWarnings, "Number of nullable arithmetic warnings");
STATISTIC(NumReturnWarnings, "Number of nullable return warnings");
STATISTIC(NumAssignmentWarnings, "Number of nullable assignment warnings");
STATISTIC(NumArgumentWarnings, "Number of nullable argument warnings");

using namespace clang;

FlowNullabilityHandler::~FlowNullabilityHandler() = default;

namespace {

using MemberKey = std::pair<const VarDecl *, const FieldDecl *>;

/// Per-block dataflow lattice tracking which pointers are narrowed (known
/// non-null) or nullable. Uses DenseSet for simplicity; a BitVector keyed
/// by variable index would reduce fixpoint comparison cost for functions
/// with many tracked pointers, but profiling hasn't shown this to be a
/// bottleneck in practice (the perf stress test passes comfortably).
struct NullState {
  // Pointers proven non-null by control flow (null checks, nonnull init, etc.).
  // A variable should not be in both NarrowedVars and NullableVars — narrowing
  // is always erased before re-evaluating nullability on reassignment.
  llvm::DenseSet<const VarDecl *> NarrowedVars;
  llvm::DenseSet<MemberKey> NarrowedMembers;
  llvm::DenseSet<const FieldDecl *> NarrowedThisMembers;
  llvm::DenseSet<const VarDecl *> NullableVars;
  // Smart pointer this-members known to be nullable in the current function
  // (e.g., after reset() or std::move()). Used to avoid false positives on
  // member smart pointers that are always initialized in the constructor.
  llvm::DenseSet<const FieldDecl *> NullableThisMembers;

  // Maps bool variables to the null-check they capture.
  // E.g., bool valid = (p != nullptr) → {valid → (p, false)}
  // The bool is true when the bool being true means the pointer IS null.
  using BoolGuardMap =
      llvm::DenseMap<const VarDecl *, std::pair<const VarDecl *, bool>>;
  BoolGuardMap BoolGuards;

  // Simple pointer alias tracking: y = x stores {y → x}, meaning y holds
  // the same pointer value as x. When either is narrowed by a branch
  // condition, the other is narrowed too (at the edge-state level).
  // Depth-1 only: if z = y and y → x, we store z → x (canonical target).
  using AliasMap = llvm::DenseMap<const VarDecl *, const VarDecl *>;
  AliasMap Aliases;

  bool operator==(const NullState &Other) const {
    return NarrowedVars == Other.NarrowedVars &&
           NarrowedMembers == Other.NarrowedMembers &&
           NarrowedThisMembers == Other.NarrowedThisMembers &&
           NullableVars == Other.NullableVars &&
           NullableThisMembers == Other.NullableThisMembers &&
           BoolGuards == Other.BoolGuards && Aliases == Other.Aliases;
  }
  bool operator!=(const NullState &Other) const { return !(*this == Other); }
};

static NullState join(const NullState &A, const NullState &B) {
  NullState Result;
  // Narrowed = intersection: only narrowed if ALL paths agree.
  for (const auto *VD : A.NarrowedVars)
    if (B.NarrowedVars.contains(VD))
      Result.NarrowedVars.insert(VD);
  for (const auto &MK : A.NarrowedMembers)
    if (B.NarrowedMembers.contains(MK))
      Result.NarrowedMembers.insert(MK);
  for (const auto *FD : A.NarrowedThisMembers)
    if (B.NarrowedThisMembers.contains(FD))
      Result.NarrowedThisMembers.insert(FD);
  // Nullable = union: if nullable on either path, it's nullable.
  for (const auto *VD : A.NullableVars)
    Result.NullableVars.insert(VD);
  for (const auto *VD : B.NullableVars)
    Result.NullableVars.insert(VD);
  for (const auto *FD : A.NullableThisMembers)
    Result.NullableThisMembers.insert(FD);
  for (const auto *FD : B.NullableThisMembers)
    Result.NullableThisMembers.insert(FD);
  // BoolGuards: keep only entries present in both with the same mapping.
  for (const auto &[BoolVD, GuardInfo] : A.BoolGuards) {
    auto It = B.BoolGuards.find(BoolVD);
    if (It != B.BoolGuards.end() && It->second == GuardInfo)
      Result.BoolGuards[BoolVD] = GuardInfo;
  }
  // Aliases: intersection with value equality (same as BoolGuards).
  for (const auto &[AliasVD, TargetVD] : A.Aliases) {
    auto It = B.Aliases.find(AliasVD);
    if (It != B.Aliases.end() && It->second == TargetVD)
      Result.Aliases[AliasVD] = TargetVD;
  }
  // Invariant: a variable should not be both narrowed and nullable.
  // Narrowed takes priority (proven non-null on all paths), so remove
  // stale nullable entries that conflict. This prevents NullableVars
  // from accumulating stale entries across fixpoint iterations.
  for (const auto *VD : Result.NarrowedVars)
    Result.NullableVars.erase(VD);
  LLVM_DEBUG({
    llvm::dbgs() << "  join: narrowed=" << Result.NarrowedVars.size()
                 << " nullable=" << Result.NullableVars.size()
                 << " members=" << Result.NarrowedMembers.size()
                 << " aliases=" << Result.Aliases.size() << "\n";
  });
  return Result;
}

static const Expr *unwrapBuiltinExpect(const Expr *E) {
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const auto *Callee = CE->getDirectCallee()) {
      unsigned BuiltinID = Callee->getBuiltinID();
      if ((BuiltinID == Builtin::BI__builtin_expect ||
           BuiltinID == Builtin::BI__builtin_expect_with_probability) &&
          CE->getNumArgs() >= 1) {
        return CE->getArg(0)->IgnoreParenImpCasts();
      }
    }
  }
  return E;
}

/// Extract the rightmost leaf of a && / || chain.
/// The CFG decomposes `a && b && c` into separate blocks — each operand
/// becomes its own block's terminator condition. So for `if (a && b && c)`,
/// the block evaluating 'c' has the full `a && b && c` as its terminator,
/// but 'a' and 'b' are handled by their own blocks. We recurse into the
/// RHS to find the leaf that's actually being evaluated in this block.
static const Expr *getTerminalCondition(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
      return getTerminalCondition(BO->getRHS());
  }
  return E;
}

static bool isNullableType(QualType Ty, bool StrictMode,
                           NullabilityKind Default) {
  std::optional<NullabilityKind> Nullability = Ty->getNullability();
  if (!Nullability)
    return false;
  // Explicit _Nullable always triggers.
  if (*Nullability == NullabilityKind::Nullable)
    return true;
  // _Null_unspecified means "not explicitly annotated — use the default".
  // Under -fnullability-default=nullable, treat as nullable.
  // Under -fnullability-default=nonnull, treat as nonnull (no warning).
  if (*Nullability == NullabilityKind::Unspecified &&
      Default == NullabilityKind::Nullable)
    return true;
  return false;
}

static bool isNonnullType(QualType Ty) {
  std::optional<NullabilityKind> Nullability = Ty->getNullability();
  return Nullability && *Nullability == NullabilityKind::NonNull;
}

/// Check if a type is std::unique_ptr, std::shared_ptr, or std::weak_ptr.
/// Uses getAsCXXRecordDecl() which operates on the canonical type, so
/// type aliases (using/typedef) are handled. Does not match non-std
/// smart pointers (e.g. boost::shared_ptr).
static bool isSmartPointerType(QualType Ty) {
  const auto *RD = Ty->getAsCXXRecordDecl();
  if (!RD)
    return false;
  const auto *DC = RD->getDeclContext();
  if (!DC || !DC->isStdNamespace())
    return false;
  StringRef Name = RD->getName();
  return Name == "unique_ptr" || Name == "shared_ptr" || Name == "weak_ptr";
}

/// Check if a smart pointer expression (the implicit object of operator->)
/// is narrowed in the current state.
static bool isSmartPointerNarrowed(const Expr *E, const NullState &State) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      return State.NarrowedVars.contains(VD);
  } else if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Base))
        return State.NarrowedThisMembers.contains(FD);
      if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base))
        if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl()))
          return State.NarrowedMembers.contains({BaseVD, FD});
    }
  }
  return false;
}

/// Strip implicit wrappers that real standard library headers introduce
/// around expressions (ExprWithCleanups, CXXBindTemporaryExpr,
/// MaterializeTemporaryExpr) plus the usual parens and implicit casts.
/// Test mocks don't produce these wrappers, but real <memory> does.
static const Expr *unwrapImplicitWrappers(const Expr *E) {
  while (true) {
    E = E->IgnoreParenImpCasts();
    if (const auto *EWC = dyn_cast<ExprWithCleanups>(E))
      E = EWC->getSubExpr();
    else if (const auto *BTE = dyn_cast<CXXBindTemporaryExpr>(E))
      E = BTE->getSubExpr();
    else if (const auto *MTE = dyn_cast<MaterializeTemporaryExpr>(E))
      E = MTE->getSubExpr();
    else
      break;
  }
  return E;
}

/// Check if a callee is std::make_unique or std::make_shared.
static bool isMakeSmartPtrCall(const Expr *E) {
  E = unwrapImplicitWrappers(E);
  if (const auto *CE = dyn_cast<CXXConstructExpr>(E)) {
    if (CE->getNumArgs() == 1)
      return isMakeSmartPtrCall(CE->getArg(0));
  }
  if (const auto *CE = dyn_cast<CallExpr>(E)) {
    if (const auto *Callee = CE->getDirectCallee()) {
      const auto *DC = Callee->getDeclContext();
      if (DC && DC->isStdNamespace() && Callee->getDeclName().isIdentifier()) {
        StringRef Name = Callee->getName();
        return Name == "make_unique" || Name == "make_shared";
      }
    }
  }
  return false;
}

/// Get the VarDecl from a smart pointer expression, if it's a simple
/// DeclRefExpr to a VarDecl.
static const VarDecl *getSmartPtrVarDecl(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *DRE = dyn_cast<DeclRefExpr>(E))
    if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
      if (isSmartPointerType(VD->getType()))
        return VD;
  return nullptr;
}

/// Get the FieldDecl from a smart pointer this->member expression.
static const FieldDecl *getSmartPtrThisMemberDecl(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
    if (isa<CXXThisExpr>(Base))
      if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl()))
        if (isSmartPointerType(FD->getType()))
          return FD;
  }
  return nullptr;
}

struct ConditionResult {
  const VarDecl *VD = nullptr;
  const FieldDecl *FD = nullptr;
  bool IsThisMember = false;
  bool Negated = false;
};

// Forward declaration — decomposeAnd calls analyzeCondition on leaves.
static void
analyzeCondition(const Expr *Cond, ASTContext &Ctx,
                 SmallVectorImpl<ConditionResult> &Results,
                 const NullState::BoolGuardMap *BoolGuards = nullptr);

/// Recursively flatten a chain of && operators and analyze each leaf.
/// Used by analyzeCondition to handle !(A && B && C).
static void decomposeAnd(const Expr *E, ASTContext &Ctx,
                         SmallVectorImpl<ConditionResult> &Results,
                         const NullState::BoolGuardMap *BoolGuards) {
  E = E->IgnoreParenImpCasts();
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd) {
      decomposeAnd(BO->getLHS(), Ctx, Results, BoolGuards);
      decomposeAnd(BO->getRHS(), Ctx, Results, BoolGuards);
      return;
    }
  }
  analyzeCondition(E, Ctx, Results, BoolGuards);
}

/// Analyze a branch condition to extract pointer null-check information.
///
/// Note: We decompose && (via decomposeAnd) but intentionally do NOT
/// decompose ||. For || the CFG already splits each operand into its own
/// block, so narrowing on the true-edge of individual operands is handled
/// naturally. Decomposing || on the false-edge (where all operands are
/// false) would be possible but adds complexity for limited practical gain
/// — most real null-checks use && or standalone conditions.
static void analyzeCondition(const Expr *Cond, ASTContext &Ctx,
                             SmallVectorImpl<ConditionResult> &Results,
                             const NullState::BoolGuardMap *BoolGuards) {
  if (!Cond)
    return;

  const Expr *E = Cond->IgnoreParenImpCasts();
  E = unwrapBuiltinExpect(E);

  bool Negated = false;
  while (auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() != UO_LNot)
      break;
    Negated = !Negated;
    E = UO->getSubExpr()->IgnoreParenImpCasts();
  }

  // !(A && B): the CFG merges the && operand paths before the if-decision,
  // so individual narrowing from the && blocks is lost at the merge.
  // Recursively decompose the && to narrow ALL operands on the false edge
  // (where && was true → all operands are true → all pointers non-null).
  if (Negated) {
    if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
      if (BO->getOpcode() == BO_LAnd) {
        // Flatten nested && and analyze each leaf
        decomposeAnd(BO, Ctx, Results, BoolGuards);
        // Keep only sub-conditions where the pointer is non-null when the
        // sub-condition is true (Negated=false). Flip to Negated=true so
        // narrowing lands on the false edge of the outer !.
        llvm::erase_if(Results,
                       [](const ConditionResult &CR) { return CR.Negated; });
        for (auto &CR : Results)
          CR.Negated = true;
        return;
      }
    }
  }

  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_NE || BO->getOpcode() == BO_EQ) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

      bool LHSIsNull =
          LHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull);
      bool RHSIsNull =
          RHS->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull);

      if (LHSIsNull || RHSIsNull) {
        const Expr *PtrExpr = LHSIsNull ? RHS : LHS;
        bool EqNegated = Negated;
        if (BO->getOpcode() == BO_EQ)
          EqNegated = !EqNegated;

        // Unwrap assignment-in-condition: (p = f()) != nullptr → narrow p
        if (const auto *AssignBO = dyn_cast<BinaryOperator>(PtrExpr)) {
          if (AssignBO->getOpcode() == BO_Assign)
            PtrExpr = AssignBO->getLHS()->IgnoreParenImpCasts();
        }

        if (const auto *DRE = dyn_cast<DeclRefExpr>(PtrExpr)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            Results.push_back({VD, nullptr, false, EqNegated});
            return;
          }
        }
        if (const auto *ME = dyn_cast<MemberExpr>(PtrExpr)) {
          if (ME->getType()->isPointerType()) {
            const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
            if (isa<CXXThisExpr>(Base)) {
              if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
                Results.push_back({nullptr, FD, true, EqNegated});
                return;
              }
            }
            if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base)) {
              if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl())) {
                if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
                  Results.push_back({BaseVD, FD, false, EqNegated});
                  return;
                }
              }
            }
          }
        }
      }
      return;
    }
  }

  if (const auto *UO = dyn_cast<UnaryOperator>(E)) {
    if (UO->getOpcode() == UO_Deref) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
      if (auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getType()->isPointerType()) {
            Results.push_back({VD, nullptr, false, Negated});
            return;
          }
        }
      }
    }
  }

  // Unwrap assignment-in-condition for truthiness: while ((p = f())) → p
  if (const auto *AssignBO = dyn_cast<BinaryOperator>(E)) {
    if (AssignBO->getOpcode() == BO_Assign)
      E = AssignBO->getLHS()->IgnoreParenImpCasts();
  }

  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (VD->getType()->isPointerType()) {
        Results.push_back({VD, nullptr, false, Negated});
        return;
      }
      // Bool intermediary: if (valid) where valid = (p != nullptr)
      if (BoolGuards && VD->getType()->isBooleanType()) {
        auto It = BoolGuards->find(VD);
        if (It != BoolGuards->end()) {
          // XOR: outer ! flips the guard's sense
          Results.push_back(
              {It->second.first, nullptr, false, Negated != It->second.second});
          return;
        }
      }
    }
  }

  if (const auto *ME = dyn_cast<MemberExpr>(E)) {
    if (ME->getType()->isPointerType()) {
      const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
      if (isa<CXXThisExpr>(Base)) {
        if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
          Results.push_back({nullptr, FD, true, Negated});
          return;
        }
      }
      if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base)) {
        if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl())) {
          if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
            Results.push_back({BaseVD, FD, false, Negated});
            return;
          }
        }
      }
    }
  }

  // Handle smart pointer implicit bool conversion: if (sp) { ... }
  // The AST represents this as a CXXMemberCallExpr to operator bool().
  if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(E)) {
    if (const auto *CD =
            dyn_cast_or_null<CXXConversionDecl>(MCE->getMethodDecl())) {
      if (CD->getConversionType()->isBooleanType()) {
        const Expr *Obj = MCE->getImplicitObjectArgument();
        if (Obj && isSmartPointerType(Obj->getType())) {
          Obj = Obj->IgnoreParenImpCasts();
          if (const auto *DRE = dyn_cast<DeclRefExpr>(Obj)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
              Results.push_back({VD, nullptr, false, Negated});
              return;
            }
          }
          if (const auto *ObjME = dyn_cast<MemberExpr>(Obj)) {
            if (const auto *FD = dyn_cast<FieldDecl>(ObjME->getMemberDecl())) {
              const Expr *ObjBase = ObjME->getBase()->IgnoreParenImpCasts();
              if (isa<CXXThisExpr>(ObjBase)) {
                Results.push_back({nullptr, FD, true, Negated});
                return;
              }
              if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(ObjBase)) {
                if (const auto *BaseVD =
                        dyn_cast<VarDecl>(BaseDRE->getDecl())) {
                  Results.push_back({BaseVD, FD, false, Negated});
                  return;
                }
              }
            }
          }
        }
      }
    }
  }
}

/// Transfer functions for the flow-sensitive nullability dataflow analysis.
/// Processes each CFG statement to update the NullState lattice — tracking
/// narrowing from null checks, invalidation from assignments, and reporting
/// dereferences of nullable pointers via the Handler interface.
class TransferFunctions {
  NullState &State;
  FlowNullabilityHandler &Handler;
  ASTContext &Ctx;
  bool StrictMode;
  NullabilityKind DefaultNullability;

  bool isNarrowed(const VarDecl *VD) const {
    return State.NarrowedVars.contains(VD);
  }

  bool isMemberNarrowed(const VarDecl *BaseVD, const FieldDecl *FD) const {
    return State.NarrowedMembers.contains({BaseVD, FD});
  }

  bool isThisMemberNarrowed(const FieldDecl *FD) const {
    return State.NarrowedThisMembers.contains(FD);
  }

  /// Unwrap explicit casts and pointer arithmetic to find the original
  /// pointer expression and whether a cast was traversed.  Template
  /// instantiations can bake _Nullable into cast result types even when
  /// the source is unannotated (e.g. reinterpret_cast<T*>(p) where T
  /// is itself a pointer type).  When a cast is found, callers should
  /// check nullability on the SOURCE type, not the cast result.
  static const Expr *unwrapCastsAndArithmetic(const Expr *E, bool &FoundCast) {
    FoundCast = false;
    for (;;) {
      if (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
        FoundCast = true;
        E = CE->getSubExpr()->IgnoreParenImpCasts();
      } else if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
        if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
          E = BO->getLHS()->getType()->isPointerType()
                  ? BO->getLHS()->IgnoreParenImpCasts()
                  : BO->getRHS()->IgnoreParenImpCasts();
        } else {
          break;
        }
      } else {
        break;
      }
    }
    return E;
  }

  void checkDeref(const Expr *DerefExpr, QualType PtrType) {
    if (isNullableType(PtrType, StrictMode, DefaultNullability)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "  deref: nullable " << PtrType.getAsString() << "\n");
      ++NumDereferenceWarnings;
      Handler.handleNullableDereference(DerefExpr, PtrType);
    }
  }

  /// Check dereference of a non-variable, non-member expression.
  /// Unwraps casts/arithmetic to avoid template-instantiation false
  /// positives where _Nullable is baked into cast result types.
  void checkExprDeref(const Expr *DerefExpr, const Expr *PtrExpr) {
    bool FoundCast = false;
    const Expr *Origin = unwrapCastsAndArithmetic(PtrExpr, FoundCast);

    // If the origin is inherently non-null, skip.
    if (isa<CXXThisExpr>(Origin))
      return;
    if (const auto *UO = dyn_cast<UnaryOperator>(Origin))
      if (UO->getOpcode() == UO_AddrOf)
        return;

    // Check member narrowing: this->member or var.member
    if (const auto *ME = dyn_cast<MemberExpr>(Origin)) {
      if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
        const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
        if (isa<CXXThisExpr>(Base) && isThisMemberNarrowed(FD))
          return;
        if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base))
          if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl()))
            if (isMemberNarrowed(BaseVD, FD))
              return;
      }
    }

    QualType CheckTy = FoundCast ? Origin->getType() : PtrExpr->getType();
    checkDeref(DerefExpr, CheckTy);
  }

  void checkVarDeref(const Expr *DerefExpr, const VarDecl *VD) {
    QualType Ty = VD->getType();
    if (isNullableType(Ty, StrictMode, DefaultNullability) ||
        State.NullableVars.contains(VD)) {
      LLVM_DEBUG(llvm::dbgs()
                 << "  deref: var '" << VD->getNameAsString() << "'\n");
      ++NumDereferenceWarnings;
      return Handler.handleNullableDereference(DerefExpr, Ty);
    }
  }

  /// Warn on smart pointer dereference. For local vars/params, always warn
  /// (they're nullable by default). For this->member smart pointers, only warn
  /// if there's evidence of nullability in the current function (reset, move,
  /// or null check) to avoid false positives on members set in constructors.
  void warnSmartPtrDeref(const Expr *DerefExpr, const Expr *Obj) {
    Obj = Obj->IgnoreParenImpCasts();
    // Local variable or parameter — always warn when not narrowed
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Obj)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        LLVM_DEBUG(llvm::dbgs()
                   << "  deref: smart ptr '" << VD->getNameAsString() << "'\n");
        ++NumDereferenceWarnings;
        Handler.handleNullableDereference(DerefExpr, VD->getType());
        return;
      }
    }
    // this->member — only warn if known nullable in current function
    if (const auto *FD = getSmartPtrThisMemberDecl(Obj)) {
      if (State.NullableThisMembers.contains(FD)) {
        ++NumDereferenceWarnings;
        Handler.handleNullableDereference(DerefExpr, FD->getType());
      }
    }
  }

  /// Remove any BoolGuards that reference the given pointer variable.
  void invalidateBoolGuardsFor(const VarDecl *VD) {
    SmallVector<const VarDecl *, 2> ToRemove;
    for (const auto &[BoolVD, GuardInfo] : State.BoolGuards)
      if (GuardInfo.first == VD)
        ToRemove.push_back(BoolVD);
    for (const auto *BoolVD : ToRemove)
      State.BoolGuards.erase(BoolVD);
  }

  /// Remove any Aliases that target the given pointer variable (the alias
  /// source was reassigned, so copies of its old value are stale).
  void invalidateAliasesFor(const VarDecl *VD) {
    SmallVector<const VarDecl *, 2> ToRemove;
    for (const auto &[AliasVD, TargetVD] : State.Aliases)
      if (TargetVD == VD)
        ToRemove.push_back(AliasVD);
    for (const auto *AliasVD : ToRemove)
      State.Aliases.erase(AliasVD);
  }

  /// Resolve a VarDecl through the alias chain to its canonical target.
  /// Returns VD itself if it's not an alias of anything.
  const VarDecl *resolveAlias(const VarDecl *VD) const {
    auto It = State.Aliases.find(VD);
    return It != State.Aliases.end() ? It->second : VD;
  }

  void invalidateMembersFor(const VarDecl *VD) {
    SmallVector<MemberKey, 4> ToRemove;
    for (const auto &MK : State.NarrowedMembers)
      if (MK.first == VD)
        ToRemove.push_back(MK);
    for (const auto &MK : ToRemove)
      State.NarrowedMembers.erase(MK);
  }

public:
  TransferFunctions(NullState &State, FlowNullabilityHandler &Handler,
                    ASTContext &Ctx, bool StrictMode,
                    NullabilityKind DefaultNullability)
      : State(State), Handler(Handler), Ctx(Ctx), StrictMode(StrictMode),
        DefaultNullability(DefaultNullability) {}

  // The enclosing function declaration, needed for return type checking.
  const FunctionDecl *EnclosingFunc = nullptr;

  void setEnclosingFunc(const FunctionDecl *FD) { EnclosingFunc = FD; }

  void visit(const Stmt *S) {
    if (!S)
      return;

    if (const auto *DS = dyn_cast<DeclStmt>(S))
      handleDeclStmt(DS);
    else if (const auto *BO = dyn_cast<BinaryOperator>(S))
      handleBinaryOperator(BO);
    else if (const auto *UO = dyn_cast<UnaryOperator>(S))
      handleUnaryOperator(UO);
    else if (const auto *ME = dyn_cast<MemberExpr>(S))
      handleMemberExpr(ME);
    else if (const auto *ASE = dyn_cast<ArraySubscriptExpr>(S))
      handleArraySubscript(ASE);
    else if (const auto *CE = dyn_cast<CallExpr>(S))
      handleCallExpr(CE);
    else if (const auto *RS = dyn_cast<ReturnStmt>(S))
      handleReturnStmt(RS);
  }

private:
  void handleDeclStmt(const DeclStmt *DS) {
    for (const auto *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        // Track raw pointer initialization
        if (VD->getType()->isPointerType()) {
          // Alias tracking: int *y = x → {y → canonical(x)}
          if (VD->hasInit()) {
            const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
            if (const auto *InitDRE = dyn_cast<DeclRefExpr>(Init)) {
              if (const auto *InitVD = dyn_cast<VarDecl>(InitDRE->getDecl())) {
                if (InitVD->getType()->isPointerType())
                  State.Aliases[VD] = resolveAlias(InitVD);
              }
            }
          }
          if (isNonnullType(VD->getType())) {
            State.NarrowedVars.insert(VD);
            // Flow-sensitive assignment check: warn when initializing a
            // _Nonnull variable with a nullable value.
            if (VD->hasInit()) {
              const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
              // Don't warn if the init is provably non-null via narrowing.
              bool InitIsNarrowed = false;
              if (const auto *InitDRE = dyn_cast<DeclRefExpr>(Init))
                if (const auto *InitVD = dyn_cast<VarDecl>(InitDRE->getDecl()))
                  InitIsNarrowed = isNarrowed(InitVD);
              if (!InitIsNarrowed && !isNonnullInit(Init) &&
                  !isNonnullType(Init->getType()) &&
                  (isNullableType(Init->getType(), StrictMode,
                                  DefaultNullability) ||
                   isNullableInit(Init))) {
                ++NumAssignmentWarnings;
                Handler.handleNullableAssignment(VD->getInit(), VD);
              }
            }
          } else if (VD->hasInit()) {
            const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
            if (const auto *UO = dyn_cast<UnaryOperator>(Init)) {
              if (UO->getOpcode() == UO_AddrOf)
                State.NarrowedVars.insert(VD);
            } else if (isNonnullInit(Init) || isNonnullType(Init->getType())) {
              State.NarrowedVars.insert(VD);
            } else {
              // Unwrap explicit casts to check the SOURCE type, not the
              // cast result type. Template instantiations can bake
              // _Nullable into cast result types even when the source is
              // unannotated (e.g. static_cast<T*>(void_ptr)).
              const Expr *TypeExpr = Init;
              bool HasCast = false;
              while (const auto *CE = dyn_cast<ExplicitCastExpr>(TypeExpr)) {
                HasCast = true;
                TypeExpr = CE->getSubExpr()->IgnoreParenImpCasts();
              }
              if (isNullableType(TypeExpr->getType(), StrictMode,
                                 DefaultNullability) ||
                  isNullableInit(Init)) {
                State.NullableVars.insert(VD);
              } else if (HasCast) {
                // The cast source is not nullable — narrow the var to
                // override any _Nullable baked into the var's own type
                // by template instantiation.
                State.NarrowedVars.insert(VD);
              }
            }
          }
          continue;
        }

        // Track smart pointer initialization
        if (isSmartPointerType(VD->getType()) && VD->hasInit()) {
          const Expr *Init = unwrapImplicitWrappers(VD->getInit());
          if (isMakeSmartPtrCall(Init)) {
            // make_unique/make_shared always return non-null
            State.NarrowedVars.insert(VD);
          }
          // Default-constructed, nullptr, or moved-from → nullable (don't
          // narrow)
        }

        // Track bool variables assigned from null-comparisons so that
        // boolean intermediaries like bool valid = (p != nullptr) can
        // later narrow p when used as a condition.
        if (VD->getType()->isBooleanType() && VD->hasInit()) {
          const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
          SmallVector<ConditionResult, 2> InitResults;
          analyzeCondition(Init, Ctx, InitResults);
          if (InitResults.size() == 1 && InitResults[0].VD &&
              !InitResults[0].FD)
            State.BoolGuards[VD] = {InitResults[0].VD, InitResults[0].Negated};
        }
      }
    }
  }

  /// Check if an init expression is provably non-null (address-of, new,
  /// this, _Nonnull typed, narrowed var, cast of non-null, pointer arith).
  /// See also: isExprProvablyNonnull() in Sema.cpp, which is a similar
  /// heuristic used to suppress nullable-to-nonnull conversion warnings.
  bool isNonnullInit(const Expr *Init) const {
    if (!Init)
      return false;
    Init = Init->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        if (isNonnullType(VD->getType()) || isNarrowed(VD))
          return true;
    }
    // Throwing operator new never returns null.
    if (const auto *NE = dyn_cast<CXXNewExpr>(Init)) {
      if (!NE->shouldNullCheckAllocation())
        return true;
    }
    // Look through explicit casts — they don't change null/nonnull status.
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(Init))
      return isNonnullInit(CE->getSubExpr());
    // this is always non-null.
    if (isa<CXXThisExpr>(Init))
      return true;
    // Pointer arithmetic on a non-null pointer is non-null.
    if (const auto *BO = dyn_cast<BinaryOperator>(Init)) {
      if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
        if (BO->getLHS()->getType()->isPointerType())
          return isNonnullInit(BO->getLHS()->IgnoreParenImpCasts());
        if (BO->getRHS()->getType()->isPointerType())
          return isNonnullInit(BO->getRHS()->IgnoreParenImpCasts());
      }
    }
    return false;
  }

  /// Check if an init expression is nullable — either by type or because it
  /// refers to a variable known to be nullable.  Unwraps casts to propagate
  /// nullability through cast chains (e.g., `(Derived *)nullableBase`).
  bool isNullableInit(const Expr *Init) const {
    if (!Init)
      return false;
    Init = Init->IgnoreParenImpCasts();
    if (!Init)
      return false;
    if (const auto *CE = dyn_cast<ExplicitCastExpr>(Init))
      return isNullableInit(CE->getSubExpr());
    if (isNullableType(Init->getType(), StrictMode, DefaultNullability))
      return true;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        return State.NullableVars.contains(VD);
    }
    // nothrow new can return null.
    if (const auto *NE = dyn_cast<CXXNewExpr>(Init))
      return NE->shouldNullCheckAllocation();
    return false;
  }

  void handleBinaryOperator(const BinaryOperator *BO) {
    // Pointer arithmetic: p + i, i + p, p - i, p - p
    // Warn if a nullable pointer is used in arithmetic (implies it must be
    // valid). p + 0 and p - 0 are excluded as safe identity operations.
    if (BO->getOpcode() == BO_Add || BO->getOpcode() == BO_Sub) {
      const Expr *PtrExpr = nullptr;
      const Expr *OtherExpr = nullptr;
      if (BO->getLHS()->getType()->isPointerType()) {
        PtrExpr = BO->getLHS()->IgnoreParenImpCasts();
        OtherExpr = BO->getRHS()->IgnoreParenImpCasts();
      } else if (BO->getRHS()->getType()->isPointerType()) {
        PtrExpr = BO->getRHS()->IgnoreParenImpCasts();
        OtherExpr = BO->getLHS()->IgnoreParenImpCasts();
      }
      if (PtrExpr) {
        bool IsZeroOffset = false;
        if (OtherExpr && !OtherExpr->getType()->isPointerType()) {
          if (auto Val = OtherExpr->getIntegerConstantExpr(Ctx))
            if (*Val == 0)
              IsZeroOffset = true;
        }
        if (!IsZeroOffset) {
          if (const auto *DRE = dyn_cast<DeclRefExpr>(PtrExpr)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
              if (!isNarrowed(VD) && (isNullableType(VD->getType(), StrictMode,
                                                     DefaultNullability) ||
                                      State.NullableVars.contains(VD))) {
                ++NumArithmeticWarnings;
                Handler.handleNullableArithmetic(BO, VD->getType());
              }
            }
          }
        }
      }
    }

    // Compound pointer arithmetic: p += i, p -= i
    if (BO->getOpcode() == BO_AddAssign || BO->getOpcode() == BO_SubAssign) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      if (LHS->getType()->isPointerType()) {
        if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
          if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
            if (!isNarrowed(VD) && (isNullableType(VD->getType(), StrictMode,
                                                   DefaultNullability) ||
                                    State.NullableVars.contains(VD))) {
              ++NumArithmeticWarnings;
              Handler.handleNullableArithmetic(BO, VD->getType());
            }
          }
        }
      }
    }

    if (BO->isAssignmentOp()) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();

      // Assignment to a member (this->field, var->field, or s.field)
      // invalidates any narrowing on that member.
      if (const auto *ME = dyn_cast<MemberExpr>(LHS)) {
        if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
          const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
          if (ME->isArrow()) {
            if (isa<CXXThisExpr>(Base)) {
              State.NarrowedThisMembers.erase(FD);
              State.NullableThisMembers.erase(FD);
            } else if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base)) {
              if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl()))
                State.NarrowedMembers.erase({BaseVD, FD});
            }
          } else {
            // Dot access: s.field = ... invalidates narrowing on s.field
            if (isa<CXXThisExpr>(Base)) {
              State.NarrowedThisMembers.erase(FD);
              State.NullableThisMembers.erase(FD);
            } else if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base)) {
              if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl()))
                State.NarrowedMembers.erase({BaseVD, FD});
            }
          }
        }
      }

      if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          // Bool reassignment invalidates any stored guard
          if (VD->getType()->isBooleanType()) {
            State.BoolGuards.erase(VD);
            return;
          }
          if (!VD->getType()->isPointerType())
            return;
          State.NarrowedVars.erase(VD);
          State.NullableVars.erase(VD);
          invalidateMembersFor(VD);
          invalidateBoolGuardsFor(VD);
          // Invalidate aliases: VD is being reassigned, so any alias
          // pointing TO VD (i.e., "other = VD" from earlier) is stale.
          invalidateAliasesFor(VD);
          State.Aliases.erase(VD);

          if (BO->getOpcode() == BO_Assign) {
            const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();

            // Alias tracking: y = x → {y → canonical(x)}
            if (const auto *RHSDRE = dyn_cast<DeclRefExpr>(RHS)) {
              if (const auto *RHSVD = dyn_cast<VarDecl>(RHSDRE->getDecl())) {
                if (RHSVD->getType()->isPointerType())
                  State.Aliases[VD] = resolveAlias(RHSVD);
              }
            }

            if (const auto *RHSUO = dyn_cast<UnaryOperator>(RHS)) {
              if (RHSUO->getOpcode() == UO_AddrOf) {
                State.NarrowedVars.insert(VD);
                return;
              }
            }
            if (const auto *RHSDRE = dyn_cast<DeclRefExpr>(RHS)) {
              if (const auto *RHSVD = dyn_cast<VarDecl>(RHSDRE->getDecl())) {
                if (isNonnullType(RHSVD->getType()) || isNarrowed(RHSVD)) {
                  State.NarrowedVars.insert(VD);
                  return;
                }
              }
            }
            if (isNonnullInit(RHS)) {
              State.NarrowedVars.insert(VD);
              return;
            }
            if (isNonnullType(BO->getRHS()->getType())) {
              State.NarrowedVars.insert(VD);
            } else if (isNullableType(BO->getRHS()->getType(), StrictMode,
                                      DefaultNullability) ||
                       isNullableInit(RHS)) {
              State.NullableVars.insert(VD);
              // Flow-sensitive assignment check: warn when assigning a
              // nullable value to a _Nonnull variable.
              if (isNonnullType(VD->getType())) {
                ++NumAssignmentWarnings;
                Handler.handleNullableAssignment(BO, VD);
              }
            }
          }
        }
      }
    }
  }

  void handleUnaryOperator(const UnaryOperator *UO) {
    if (UO->getOpcode() == UO_Deref) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();

      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (!VD->isImplicit() && !isNarrowed(VD))
            checkVarDeref(UO, VD);
        }
      } else if (const auto *ME = dyn_cast<MemberExpr>(SubExpr)) {
        const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
        if (isa<CXXThisExpr>(Base)) {
          if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
            if (!isThisMemberNarrowed(FD))
              checkDeref(UO, ME->getType());
          }
        } else {
          checkMemberExprDeref(UO, ME);
        }
      } else if (!isa<CXXThisExpr>(SubExpr)) {
        checkExprDeref(UO, SubExpr);
      }
    }

    // Pointer increment/decrement (p++, ++p, p--, --p): arithmetic on a
    // nullable pointer is unsafe (implies the pointer must be valid).
    // Also invalidates member narrowing, bool guards, and aliases since
    // the pointer now points elsewhere.
    if (UO->getOpcode() == UO_PostInc || UO->getOpcode() == UO_PreInc ||
        UO->getOpcode() == UO_PostDec || UO->getOpcode() == UO_PreDec) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getType()->isPointerType()) {
            // Warn on arithmetic of non-narrowed nullable pointer
            if (!isNarrowed(VD) && (isNullableType(VD->getType(), StrictMode,
                                                   DefaultNullability) ||
                                    State.NullableVars.contains(VD))) {
              ++NumArithmeticWarnings;
              Handler.handleNullableArithmetic(UO, VD->getType());
            }
            invalidateMembersFor(VD);
            invalidateBoolGuardsFor(VD);
            invalidateAliasesFor(VD);
            State.Aliases.erase(VD);
          }
        }
      }
    }
  }

  void handleMemberExpr(const MemberExpr *ME) {
    if (!ME->isArrow())
      return;

    const Expr *Base = ME->getBase()->IgnoreParenImpCasts();

    if (isa<CXXThisExpr>(Base))
      return;

    // Handle overloaded operator-> (smart pointers, iterators, etc.)
    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(Base)) {
      if (OCE->getOperator() == OO_Arrow) {
        // For smart pointers, warn if not narrowed.
        // For non-smart-pointer types (iterators etc), skip as before.
        if (OCE->getNumArgs() >= 1) {
          const Expr *Obj = OCE->getArg(0);
          if (isSmartPointerType(Obj->getType())) {
            if (!isSmartPointerNarrowed(Obj, State))
              warnSmartPtrDeref(ME, Obj);
          }
        }
        return;
      }
    }

    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (!isNarrowed(VD))
          checkVarDeref(ME, VD);
      }
    } else if (const auto *BaseME = dyn_cast<MemberExpr>(Base)) {
      checkMemberExprDeref(ME, BaseME);
    } else {
      checkExprDeref(ME, Base);
    }
  }

  void handleArraySubscript(const ArraySubscriptExpr *ASE) {
    const Expr *Base = ASE->getBase()->IgnoreParenImpCasts();
    if (const auto *UO = dyn_cast<UnaryOperator>(Base))
      if (UO->getOpcode() == UO_AddrOf)
        return;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (!isNarrowed(VD) && !VD->getType()->isArrayType())
          checkVarDeref(ASE, VD);
      }
    } else if (const auto *ME = dyn_cast<MemberExpr>(Base)) {
      // Member pointer subscript: this->arr[i] or var.arr[i]
      // Check member narrowing before falling through to type check.
      if (!ME->getType()->isArrayType())
        checkMemberExprDeref(ASE, ME);
    } else {
      QualType BaseTy = Base->getType();
      if (!BaseTy->isArrayType())
        checkExprDeref(ASE, Base);
    }
  }

  /// Handle function calls. By design, calls do NOT invalidate pointer
  /// narrowing — even when a pointer's address is taken (&p) and passed as
  /// a T** argument. This is a pragmatic trade-off: invalidating on
  /// address-escape would produce excessive false positives on common
  /// patterns (output parameters, init functions). The same approach is
  /// used by Clang's ThreadSafety analysis.
  void handleCallExpr(const CallExpr *CE) {
    if (const auto *Callee = CE->getDirectCallee()) {
      // __builtin_assume(cond) narrows pointers mentioned in cond.
      if (Callee->getBuiltinID() == Builtin::BI__builtin_assume &&
          CE->getNumArgs() >= 1) {
        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
        SmallVector<ConditionResult, 2> Results;
        analyzeCondition(Arg, Ctx, Results, &State.BoolGuards);
        for (const auto &CR : Results) {
          if (CR.Negated)
            continue;
          if (CR.IsThisMember) {
            State.NarrowedThisMembers.insert(CR.FD);
          } else if (CR.VD) {
            if (!CR.FD)
              State.NarrowedVars.insert(CR.VD);
            else
              State.NarrowedMembers.insert({CR.VD, CR.FD});
          }
        }
      }

      // Narrow pointers passed to _Nonnull parameters — surviving the call
      // proves the pointer was non-null. Recognizes both Clang _Nonnull
      // and GCC-style __attribute__((nonnull)).
      const auto *NNAttr = Callee->getAttr<NonNullAttr>();
      for (unsigned I = 0,
                    N = std::min(CE->getNumArgs(), Callee->getNumParams());
           I < N; ++I) {
        const ParmVarDecl *Param = Callee->getParamDecl(I);
        if (!Param->getType()->isPointerType())
          continue;
        bool ParamIsNonnull =
            isNonnullType(Param->getType()) || (NNAttr && NNAttr->isNonNull(I));
        if (ParamIsNonnull) {
          const Expr *Arg = CE->getArg(I)->IgnoreParenImpCasts();
          // Flow-sensitive argument check: warn when passing a nullable
          // pointer to a _Nonnull parameter.
          if (isExprNullable(Arg)) {
            ++NumArgumentWarnings;
            Handler.handleNullableArgument(CE->getArg(I), Param);
          }
          if (const auto *DRE = dyn_cast<DeclRefExpr>(Arg)) {
            if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
              if (VD->getType()->isPointerType())
                State.NarrowedVars.insert(VD);
          }
        }
      }
    }

    // Handle sp.reset() / sp.reset(ptr) — CXXMemberCallExpr
    if (const auto *MCE = dyn_cast<CXXMemberCallExpr>(CE)) {
      const Expr *Obj = MCE->getImplicitObjectArgument();
      if (Obj && isSmartPointerType(Obj->getType())) {
        if (const auto *MD = MCE->getMethodDecl()) {
          if (MD->getDeclName().isIdentifier() && MD->getName() == "reset") {
            // reset(nullptr) makes it null; reset(ptr) makes it non-null;
            // reset() with no args makes it null.
            bool ResetsToNonnull =
                MCE->getNumArgs() > 0 &&
                !MCE->getArg(0)->IgnoreParenImpCasts()->isNullPointerConstant(
                    Ctx, Expr::NPC_ValueDependentIsNotNull);
            // Local variable
            if (const auto *VD = getSmartPtrVarDecl(Obj)) {
              State.NarrowedVars.erase(VD);
              if (ResetsToNonnull)
                State.NarrowedVars.insert(VD);
            }
            // this->member
            if (const auto *FD = getSmartPtrThisMemberDecl(Obj)) {
              State.NarrowedThisMembers.erase(FD);
              if (ResetsToNonnull) {
                State.NarrowedThisMembers.insert(FD);
                State.NullableThisMembers.erase(FD);
              } else {
                State.NullableThisMembers.insert(FD);
              }
            }
          }
        }
      }
    }

    // Handle sp = nullptr / sp = make_unique(...) / sp = std::move(other)
    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(CE)) {
      if (OCE->getOperator() == OO_Equal && OCE->getNumArgs() >= 2) {
        const VarDecl *LhsVD = getSmartPtrVarDecl(OCE->getArg(0));
        if (LhsVD) {
          State.NarrowedVars.erase(LhsVD);
          const Expr *RHS = unwrapImplicitWrappers(OCE->getArg(1));

          if (isMakeSmartPtrCall(RHS)) {
            // sp = make_unique<T>(...) — non-null
            State.NarrowedVars.insert(LhsVD);
          } else if (const auto *RhsCE = dyn_cast<CallExpr>(RHS)) {
            if (RhsCE->isCallToStdMove() && RhsCE->getNumArgs() >= 1) {
              // sp = std::move(other) — LHS inherits source's state
              if (const auto *SrcVD = getSmartPtrVarDecl(RhsCE->getArg(0))) {
                // Only narrow LHS if source was narrowed (known non-null)
                if (State.NarrowedVars.contains(SrcVD))
                  State.NarrowedVars.insert(LhsVD);
                State.NarrowedVars.erase(SrcVD);
              }
            } else if (isNonnullType(RhsCE->getType())) {
              // sp = someFunction() — only narrow if return type is _Nonnull
              State.NarrowedVars.insert(LhsVD);
            }
          }
          // sp = nullptr or non-call — remains nullable (erased above)
        }
      }
    }

    // Handle *sp (operator*) on smart pointers — same as operator->
    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(CE)) {
      if (OCE->getOperator() == OO_Star && OCE->getNumArgs() >= 1) {
        const Expr *Obj = OCE->getArg(0);
        if (isSmartPointerType(Obj->getType())) {
          if (!isSmartPointerNarrowed(Obj, State))
            warnSmartPtrDeref(CE, Obj);
        }
      }
    }

    // Handle std::move(sp) — marks the source as nullable
    if (CE->isCallToStdMove() && CE->getNumArgs() >= 1) {
      if (const auto *VD = getSmartPtrVarDecl(CE->getArg(0))) {
        State.NarrowedVars.erase(VD);
      }
      if (const auto *FD = getSmartPtrThisMemberDecl(CE->getArg(0))) {
        State.NarrowedThisMembers.erase(FD);
        State.NullableThisMembers.insert(FD);
      }
    }
  }

  /// Flow-aware nullable check: returns true if the expression is nullable
  /// considering both declared type and dynamic state (NarrowedVars,
  /// NullableVars). This goes beyond the type-based check — it respects
  /// null checks (narrowing suppresses the warning) and dynamic nullability
  /// (reset/move makes a variable nullable even if its type isn't).
  bool isVarNullable(const VarDecl *VD) const {
    if (isNarrowed(VD))
      return false;
    if (isNonnullType(VD->getType()))
      return false;
    if (isNullableType(VD->getType(), StrictMode, DefaultNullability))
      return true;
    if (State.NullableVars.contains(VD))
      return true;
    return false;
  }

  /// Check if an expression resolves to a nullable pointer, considering flow.
  bool isExprNullable(const Expr *E) const {
    if (!E)
      return false;
    E = E->IgnoreParenImpCasts();
    if (E->isNullPointerConstant(Ctx, Expr::NPC_ValueDependentIsNotNull))
      return true;
    if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        return isVarNullable(VD);
    }
    // Member narrowing: this->member or var.member narrowed by null check
    if (const auto *ME = dyn_cast<MemberExpr>(E)) {
      if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
        const Expr *Base = ME->getBase()->IgnoreParenImpCasts();
        if (isa<CXXThisExpr>(Base) && isThisMemberNarrowed(FD))
          return false;
        if (const auto *BaseDRE = dyn_cast<DeclRefExpr>(Base))
          if (const auto *BaseVD = dyn_cast<VarDecl>(BaseDRE->getDecl()))
            if (isMemberNarrowed(BaseVD, FD))
              return false;
      }
    }
    // For non-variable expressions, fall back to type-based check
    if (isNullableType(E->getType(), StrictMode, DefaultNullability))
      return true;
    return false;
  }

  void handleReturnStmt(const ReturnStmt *RS) {
    if (!EnclosingFunc)
      return;
    const Expr *RetVal = RS->getRetValue();
    if (!RetVal)
      return;
    QualType RetType = EnclosingFunc->getReturnType();
    if (!RetType->isPointerType() || !isNonnullType(RetType))
      return;
    if (isExprNullable(RetVal)) {
      ++NumReturnWarnings;
      Handler.handleNullableReturn(RetVal, RetVal->getType(), RetType);
    }
  }

  void checkMemberExprDeref(const Expr *DerefExpr, const MemberExpr *ME) {
    const Expr *Base = ME->getBase()->IgnoreParenImpCasts();

    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(Base)) {
      if (OCE->getOperator() == OO_Arrow) {
        if (OCE->getNumArgs() >= 1) {
          const Expr *Obj = OCE->getArg(0);
          if (isSmartPointerType(Obj->getType())) {
            if (!isSmartPointerNarrowed(Obj, State))
              warnSmartPtrDeref(DerefExpr, Obj);
          }
        }
        return;
      }
    }

    if (const auto *FD = dyn_cast<FieldDecl>(ME->getMemberDecl())) {
      if (isa<CXXThisExpr>(Base)) {
        if (!isThisMemberNarrowed(FD))
          checkDeref(DerefExpr, ME->getType());
      } else if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
        if (const auto *BaseVD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (!isMemberNarrowed(BaseVD, FD))
            checkDeref(DerefExpr, ME->getType());
        }
      }
    }
  }
};

} // end anonymous namespace

void clang::runFlowNullabilityAnalysis(AnalysisDeclContext &AC,
                                       FlowNullabilityHandler &Handler,
                                       bool StrictMode,
                                       NullabilityKind Default) {
  CFG *Cfg = AC.getCFG();
  if (!Cfg)
    return;

  ++NumFunctionsAnalyzed;
  ASTContext &Ctx = AC.getASTContext();
  LLVM_DEBUG({
    if (const auto *ND = dyn_cast_or_null<NamedDecl>(AC.getDecl()))
      llvm::dbgs() << "flow-nullability: analyzing '" << ND->getNameAsString()
                   << "' (" << Cfg->size() << " blocks)\n";
  });

  using EdgeKey = std::pair<unsigned, unsigned>;
  llvm::DenseMap<EdgeKey, NullState> EdgeStates;
  llvm::DenseMap<unsigned, NullState> BlockEntryStates;

  ForwardDataflowWorklist Worklist(*Cfg, AC);

  const CFGBlock &Entry = Cfg->getEntry();
  NullState InitState;

  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(AC.getDecl())) {
    for (const auto *Param : FD->parameters()) {
      if (Param->getType()->isPointerType() && isNonnullType(Param->getType()))
        InitState.NarrowedVars.insert(Param);
    }
  }

  BlockEntryStates[Entry.getBlockID()] = InitState;
  Worklist.enqueueBlock(&Entry);

  // Fixpoint iteration. Termination is guaranteed because the lattice has
  // finite height (bounded by the number of declarations in the function)
  // and the edge-state comparison ensures each block is only re-processed
  // when its entry state actually changes.
  while (const CFGBlock *Block = Worklist.dequeue()) {
    unsigned BlockID = Block->getBlockID();
    ++NumBlocksProcessed;
    ++NumFixpointIterations;
    LLVM_DEBUG(llvm::dbgs() << "  block B" << BlockID << " (preds:");

    NullState State;
    bool FirstPred = true;

    if (BlockID == Entry.getBlockID()) {
      State = BlockEntryStates[BlockID];
      FirstPred = false;
    }

    for (auto PI = Block->pred_begin(), PE = Block->pred_end(); PI != PE;
         ++PI) {
      if (const CFGBlock *Pred = *PI) {
        LLVM_DEBUG(llvm::dbgs() << " B" << Pred->getBlockID());
        EdgeKey EK = {Pred->getBlockID(), BlockID};
        auto It = EdgeStates.find(EK);
        if (It != EdgeStates.end()) {
          if (FirstPred) {
            State = It->second;
            FirstPred = false;
          } else {
            State = join(State, It->second);
          }
        }
      }
    }
    LLVM_DEBUG(llvm::dbgs() << ")\n");

    if (FirstPred)
      continue;

    // Standard fixpoint check: skip re-processing if entry state is unchanged.
    // This prevents duplicate warnings when the worklist re-visits a block.
    // Skip this check for the entry block — its state is pre-seeded, so it
    // would always match and prevent the first visit from propagating.
    if (BlockID != Entry.getBlockID()) {
      auto OldIt = BlockEntryStates.find(BlockID);
      if (OldIt != BlockEntryStates.end() && OldIt->second == State) {
        LLVM_DEBUG(llvm::dbgs() << "    converged, skipping\n");
        continue;
      }
    }
    BlockEntryStates[BlockID] = State;

    TransferFunctions TF(State, Handler, Ctx, StrictMode, Default);
    if (const auto *FD = dyn_cast_or_null<FunctionDecl>(AC.getDecl()))
      TF.setEnclosingFunc(FD);
    for (const auto &Elem : *Block) {
      if (std::optional<CFGStmt> CS = Elem.getAs<CFGStmt>())
        TF.visit(CS->getStmt());
    }

    NullState TrueState = State;
    NullState FalseState = State;

    if (const Stmt *Term = Block->getTerminatorStmt()) {
      const Expr *Cond = nullptr;
      if (const auto *IS = dyn_cast<IfStmt>(Term)) {
        const Expr *IfCond = IS->getCond();
        if (IfCond)
          IfCond = IfCond->IgnoreParenImpCasts();
        if (IfCond) {
          if (const auto *BO = dyn_cast<BinaryOperator>(IfCond)) {
            if (BO->getOpcode() == BO_LAnd) {
              SmallVector<ConditionResult, 2> AndResults;
              decomposeAnd(BO, Ctx, AndResults, &State.BoolGuards);
              for (const auto &CR : AndResults) {
                if (CR.VD && !CR.FD && !CR.IsThisMember && !CR.Negated) {
                  TrueState.NarrowedVars.insert(CR.VD);
                  // Also narrow alias target and all siblings
                  const VarDecl *Target = CR.VD;
                  auto AliasIt = TrueState.Aliases.find(CR.VD);
                  if (AliasIt != TrueState.Aliases.end()) {
                    Target = AliasIt->second;
                    TrueState.NarrowedVars.insert(Target);
                  }
                  for (const auto &[AV, TV] : TrueState.Aliases)
                    if (TV == CR.VD || TV == Target)
                      TrueState.NarrowedVars.insert(AV);
                }
              }
            }
          }
        }
        Cond = getTerminalCondition(IS->getCond());
      } else if (const auto *WS = dyn_cast<WhileStmt>(Term)) {
        Cond = getTerminalCondition(WS->getCond());
      } else if (const auto *FS = dyn_cast<ForStmt>(Term)) {
        if (FS->getCond())
          Cond = getTerminalCondition(FS->getCond());
      } else if (const auto *DS = dyn_cast<DoStmt>(Term)) {
        Cond = getTerminalCondition(DS->getCond());
      } else if (const auto *BO = dyn_cast<BinaryOperator>(Term)) {
        if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
          Cond = getTerminalCondition(BO->getLHS());
      } else if (const auto *CO = dyn_cast<ConditionalOperator>(Term)) {
        Cond = getTerminalCondition(CO->getCond());
      }

      // Propagate narrowing through aliases: when VD is narrowed on an edge,
      // also narrow its alias target and all vars sharing the same canonical
      // target. E.g., y = x; z = x; if (z) → narrow z, x, AND y.
      auto narrowWithAliases = [&](NullState &NS, const VarDecl *VD) {
        NS.NarrowedVars.insert(VD);
        NS.NullableVars.erase(VD);
        // Forward: VD aliases Target → also narrow Target
        const VarDecl *Target = VD;
        auto AliasIt = NS.Aliases.find(VD);
        if (AliasIt != NS.Aliases.end()) {
          Target = AliasIt->second;
          NS.NarrowedVars.insert(Target);
          NS.NullableVars.erase(Target);
        }
        // Reverse: narrow all vars aliasing VD or its canonical target
        for (const auto &[AliasVD, AliasTarget] : NS.Aliases) {
          if (AliasTarget == VD || AliasTarget == Target) {
            NS.NarrowedVars.insert(AliasVD);
            NS.NullableVars.erase(AliasVD);
          }
        }
      };

      if (Cond) {
        SmallVector<ConditionResult, 2> Results;
        analyzeCondition(Cond, Ctx, Results, &State.BoolGuards);
        for (const auto &CR : Results) {
          NullState &Narrow = CR.Negated ? FalseState : TrueState;
          if (CR.IsThisMember) {
            Narrow.NarrowedThisMembers.insert(CR.FD);
          } else if (CR.VD) {
            if (!CR.FD)
              narrowWithAliases(Narrow, CR.VD);
            else
              Narrow.NarrowedMembers.insert({CR.VD, CR.FD});
          }
        }
      }
    }

    unsigned SucIdx = 0;
    for (auto SI = Block->succ_begin(), SE = Block->succ_end(); SI != SE;
         ++SI, ++SucIdx) {
      if (const CFGBlock *Succ = *SI) {
        const NullState &SuccState =
            (Block->succ_size() == 2) ? (SucIdx == 0 ? TrueState : FalseState)
                                      : State;
        EdgeKey EK = {BlockID, Succ->getBlockID()};
        auto It = EdgeStates.find(EK);
        if (It == EdgeStates.end() || It->second != SuccState) {
          LLVM_DEBUG(llvm::dbgs()
                     << "    edge B" << BlockID << "->B" << Succ->getBlockID()
                     << " changed, enqueuing\n");
          EdgeStates[EK] = SuccState;
          Worklist.enqueueBlock(Succ);
        }
      }
    }
  }
}
