//===- FlowNullability.cpp - Flow-sensitive null dereference checking ------===//
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
#include "llvm/ADT/SmallVector.h"
#include <optional>
#include <utility>

using namespace clang;

FlowNullabilityHandler::~FlowNullabilityHandler() = default;

namespace {

using MemberKey = std::pair<const VarDecl *, const FieldDecl *>;

struct NullState {
  llvm::DenseSet<const VarDecl *> NarrowedVars;
  llvm::DenseSet<MemberKey> NarrowedMembers;
  llvm::DenseSet<const FieldDecl *> NarrowedThisMembers;

  bool operator==(const NullState &Other) const {
    return NarrowedVars == Other.NarrowedVars &&
           NarrowedMembers == Other.NarrowedMembers &&
           NarrowedThisMembers == Other.NarrowedThisMembers;
  }
  bool operator!=(const NullState &Other) const { return !(*this == Other); }
};

static NullState intersect(const NullState &A, const NullState &B) {
  NullState Result;
  for (const auto *VD : A.NarrowedVars)
    if (B.NarrowedVars.count(VD))
      Result.NarrowedVars.insert(VD);
  for (const auto &MK : A.NarrowedMembers)
    if (B.NarrowedMembers.count(MK))
      Result.NarrowedMembers.insert(MK);
  for (const auto *FD : A.NarrowedThisMembers)
    if (B.NarrowedThisMembers.count(FD))
      Result.NarrowedThisMembers.insert(FD);
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

static const Expr *getTerminalCondition(const Expr *E) {
  E = E->IgnoreParenImpCasts();
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
      return getTerminalCondition(BO->getRHS());
  }
  return E;
}

static bool isNullableType(QualType Ty, bool StrictMode) {
  auto Nullability = Ty->getNullability();
  if (!Nullability)
    return StrictMode;
  return *Nullability == NullabilityKind::Nullable ||
         (StrictMode && *Nullability == NullabilityKind::Unspecified);
}

static bool isNonnullType(QualType Ty) {
  auto Nullability = Ty->getNullability();
  return Nullability && *Nullability == NullabilityKind::NonNull;
}

struct ConditionResult {
  const VarDecl *VD = nullptr;
  const FieldDecl *FD = nullptr;
  bool IsThisMember = false;
  bool Negated = false;
};

static void analyzeCondition(const Expr *Cond, ASTContext &Ctx,
                             SmallVectorImpl<ConditionResult> &Results) {
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

  if (auto *DRE = dyn_cast<DeclRefExpr>(E)) {
    if (auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
      if (VD->getType()->isPointerType()) {
        Results.push_back({VD, nullptr, false, Negated});
        return;
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
}

class TransferFunctions {
  NullState &State;
  FlowNullabilityHandler &Handler;
  ASTContext &Ctx;
  bool StrictMode;

  bool isNarrowed(const VarDecl *VD) const {
    return State.NarrowedVars.count(VD);
  }

  bool isMemberNarrowed(const VarDecl *BaseVD, const FieldDecl *FD) const {
    return State.NarrowedMembers.count({BaseVD, FD});
  }

  bool isThisMemberNarrowed(const FieldDecl *FD) const {
    return State.NarrowedThisMembers.count(FD);
  }

  void checkDeref(const Expr *DerefExpr, QualType PtrType) {
    if (isNullableType(PtrType, StrictMode))
      Handler.handleNullableDereference(DerefExpr, PtrType);
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
                    ASTContext &Ctx, bool StrictMode)
      : State(State), Handler(Handler), Ctx(Ctx), StrictMode(StrictMode) {}

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
  }

private:
  void handleDeclStmt(const DeclStmt *DS) {
    for (const auto *D : DS->decls()) {
      if (const auto *VD = dyn_cast<VarDecl>(D)) {
        if (!VD->getType()->isPointerType())
          continue;
        if (isNonnullType(VD->getType())) {
          State.NarrowedVars.insert(VD);
        } else if (VD->hasInit()) {
          const Expr *Init = VD->getInit()->IgnoreParenImpCasts();
          if (const auto *UO = dyn_cast<UnaryOperator>(Init)) {
            if (UO->getOpcode() == UO_AddrOf)
              State.NarrowedVars.insert(VD);
          } else if (isNonnullInit(Init) || isNonnullType(Init->getType())) {
            State.NarrowedVars.insert(VD);
          }
        }
      }
    }
  }

  bool isNonnullInit(const Expr *Init) const {
    if (!Init)
      return false;
    Init = Init->IgnoreParenImpCasts();
    if (const auto *DRE = dyn_cast<DeclRefExpr>(Init)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        if (isNonnullType(VD->getType()) || isNarrowed(VD))
          return true;
    }
    return false;
  }

  void handleBinaryOperator(const BinaryOperator *BO) {
    if (BO->isAssignmentOp()) {
      const Expr *LHS = BO->getLHS()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(LHS)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (!VD->getType()->isPointerType())
            return;
          State.NarrowedVars.erase(VD);
          invalidateMembersFor(VD);

          if (BO->getOpcode() == BO_Assign) {
            const Expr *RHS = BO->getRHS()->IgnoreParenImpCasts();
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
            if (isNonnullType(BO->getRHS()->getType()))
              State.NarrowedVars.insert(VD);
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
            checkDeref(UO, VD->getType());
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
        checkDeref(UO, SubExpr->getType());
      }
    }

    if (UO->getOpcode() == UO_PostInc || UO->getOpcode() == UO_PreInc ||
        UO->getOpcode() == UO_PostDec || UO->getOpcode() == UO_PreDec) {
      const Expr *SubExpr = UO->getSubExpr()->IgnoreParenImpCasts();
      if (const auto *DRE = dyn_cast<DeclRefExpr>(SubExpr)) {
        if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
          if (VD->getType()->isPointerType()) {
            State.NarrowedVars.erase(VD);
            invalidateMembersFor(VD);
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

    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(Base))
      if (OCE->getOperator() == OO_Arrow)
        return;

    if (const auto *DRE = dyn_cast<DeclRefExpr>(Base)) {
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl())) {
        if (!isNarrowed(VD))
          checkDeref(ME, VD->getType());
      }
    } else if (const auto *BaseME = dyn_cast<MemberExpr>(Base)) {
      checkMemberExprDeref(ME, BaseME);
    } else {
      checkDeref(ME, Base->getType());
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
          checkDeref(ASE, VD->getType());
      }
    } else {
      QualType BaseTy = Base->getType();
      if (!BaseTy->isArrayType())
        checkDeref(ASE, BaseTy);
    }
  }

  void handleCallExpr(const CallExpr *CE) {
    if (auto *Callee = CE->getDirectCallee()) {
      if (Callee->getBuiltinID() == Builtin::BI__builtin_assume &&
          CE->getNumArgs() >= 1) {
        const Expr *Arg = CE->getArg(0)->IgnoreParenImpCasts();
        SmallVector<ConditionResult, 2> Results;
        analyzeCondition(Arg, Ctx, Results);
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
    }
  }

  void checkMemberExprDeref(const Expr *DerefExpr, const MemberExpr *ME) {
    const Expr *Base = ME->getBase()->IgnoreParenImpCasts();

    if (const auto *OCE = dyn_cast<CXXOperatorCallExpr>(Base))
      if (OCE->getOperator() == OO_Arrow)
        return;

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
  CFG *cfg = AC.getCFG();
  if (!cfg)
    return;

  ASTContext &Ctx = AC.getASTContext();
  unsigned NumBlocks = cfg->getNumBlockIDs();
  (void)NumBlocks;

  using EdgeKey = std::pair<unsigned, unsigned>;
  llvm::DenseMap<EdgeKey, NullState> EdgeStates;
  llvm::DenseMap<unsigned, NullState> BlockEntryStates;

  ForwardDataflowWorklist Worklist(*cfg, AC);

  const CFGBlock &Entry = cfg->getEntry();
  NullState InitState;

  if (const auto *FD = dyn_cast_or_null<FunctionDecl>(AC.getDecl())) {
    for (const auto *Param : FD->parameters()) {
      if (!Param->getType()->isPointerType())
        continue;
      if (isNonnullType(Param->getType())) {
        InitState.NarrowedVars.insert(Param);
      } else if (Default == NullabilityKind::NonNull &&
                 !Param->getType()->getNullability()) {
        InitState.NarrowedVars.insert(Param);
      }
    }
  }

  BlockEntryStates[Entry.getBlockID()] = InitState;
  Worklist.enqueueBlock(&Entry);

  while (const CFGBlock *Block = Worklist.dequeue()) {
    unsigned BlockID = Block->getBlockID();

    NullState State;
    bool FirstPred = true;

    if (BlockID == Entry.getBlockID()) {
      State = BlockEntryStates[BlockID];
      FirstPred = false;
    }

    for (auto PI = Block->pred_begin(), PE = Block->pred_end(); PI != PE;
         ++PI) {
      if (const CFGBlock *Pred = *PI) {
        EdgeKey EK = {Pred->getBlockID(), BlockID};
        auto It = EdgeStates.find(EK);
        if (It != EdgeStates.end()) {
          if (FirstPred) {
            State = It->second;
            FirstPred = false;
          } else {
            State = intersect(State, It->second);
          }
        }
      }
    }

    if (FirstPred)
      continue;

    NullState OldEntry;
    auto OldIt = BlockEntryStates.find(BlockID);
    if (OldIt != BlockEntryStates.end())
      OldEntry = OldIt->second;
    BlockEntryStates[BlockID] = State;

    TransferFunctions TF(State, Handler, Ctx, StrictMode);
    for (const auto &Elem : *Block) {
      if (auto CS = Elem.getAs<CFGStmt>())
        TF.visit(CS->getStmt());
    }

    NullState TrueState = State;
    NullState FalseState = State;

    if (const Stmt *Term = Block->getTerminatorStmt()) {
      const Expr *Cond = nullptr;
      if (const auto *IS = dyn_cast<IfStmt>(Term))
        Cond = getTerminalCondition(IS->getCond());
      else if (const auto *WS = dyn_cast<WhileStmt>(Term))
        Cond = getTerminalCondition(WS->getCond());
      else if (const auto *FS = dyn_cast<ForStmt>(Term)) {
        if (FS->getCond())
          Cond = getTerminalCondition(FS->getCond());
      } else if (const auto *DS = dyn_cast<DoStmt>(Term))
        Cond = getTerminalCondition(DS->getCond());
      else if (const auto *BO = dyn_cast<BinaryOperator>(Term)) {
        if (BO->getOpcode() == BO_LAnd || BO->getOpcode() == BO_LOr)
          Cond = getTerminalCondition(BO->getLHS());
      } else if (const auto *CO = dyn_cast<ConditionalOperator>(Term))
        Cond = getTerminalCondition(CO->getCond());

      if (Cond) {
        SmallVector<ConditionResult, 2> Results;
        analyzeCondition(Cond, Ctx, Results);
        for (const auto &CR : Results) {
          NullState &Narrow = CR.Negated ? FalseState : TrueState;
          if (CR.IsThisMember) {
            Narrow.NarrowedThisMembers.insert(CR.FD);
          } else if (CR.VD) {
            if (!CR.FD)
              Narrow.NarrowedVars.insert(CR.VD);
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
            (Block->succ_size() == 2)
                ? (SucIdx == 0 ? TrueState : FalseState)
                : State;
        EdgeKey EK = {BlockID, Succ->getBlockID()};
        auto It = EdgeStates.find(EK);
        if (It == EdgeStates.end() || It->second != SuccState) {
          EdgeStates[EK] = SuccState;
          Worklist.enqueueBlock(Succ);
        }
      }
    }
  }
}
