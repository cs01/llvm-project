//===- NullabilitySafetyExtractor.cpp -------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "SSAFAnalysesCommon.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Expr.h"
#include "clang/AST/ExprCXX.h"
#include "clang/Analysis/Analyses/NullabilitySafety.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Lex/Lexer.h"
#include "clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevel.h"
#include "clang/ScalableStaticAnalysis/Analyses/NullabilitySafety/NullabilitySafety.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/ExtractorRegistry.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryBuilder.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryExtractor.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include <memory>
#include <vector>

using namespace clang;
using namespace ssaf;

namespace {
struct DeclEvidence {
  std::vector<DeclPointerLevel> Nonnull;
  std::vector<DeclPointerLevel> Nullable;
  std::vector<DeclPointerLevel> AllReturnsNonnull;
  std::vector<DeclPointerLevel> MaybeNull;
};

const Decl *contributorOf(const Decl *Def) {
  const Decl *D = isa<BlockDecl>(Def) ? Def->getNonClosureContext() : Def;
  return D ? D->getCanonicalDecl() : nullptr;
}

bool isInAssertMacro(SourceLocation Loc, const ASTContext &Ctx) {
  const SourceManager &SM = Ctx.getSourceManager();
  while (Loc.isMacroID()) {
    if (Lexer::getImmediateMacroName(Loc, SM, Ctx.getLangOpts())
            .contains_insensitive("assert"))
      return true;
    Loc = SM.getImmediateMacroCallerLoc(Loc);
  }
  return false;
}

const Expr *nullTestedPointer(const Expr *E, const ASTContext &Ctx) {
  auto IsNull = [&](const Expr *Op) {
    return Op->isNullPointerConstant(const_cast<ASTContext &>(Ctx),
                                     Expr::NPC_ValueDependentIsNotNull);
  };
  if (const auto *BO = dyn_cast<BinaryOperator>(E)) {
    if (!BO->isEqualityOp())
      return nullptr;
    if (IsNull(BO->getRHS()))
      return BO->getLHS();
    if (IsNull(BO->getLHS()))
      return BO->getRHS();
    return nullptr;
  }
  if (const auto *UO = dyn_cast<UnaryOperator>(E))
    return UO->getOpcode() == UO_LNot ? UO->getSubExpr() : nullptr;
  if (const auto *ICE = dyn_cast<ImplicitCastExpr>(E))
    return ICE->getCastKind() == CK_PointerToBoolean ? ICE->getSubExpr()
                                                     : nullptr;
  return nullptr;
}

void collectVetoes(const NamedDecl *Contributor, const ASTContext &Ctx,
                   std::vector<DeclPointerLevel> &MaybeNull) {
  const auto *Func = dyn_cast<FunctionDecl>(Contributor);
  llvm::DenseMap<const VarDecl *, llvm::SmallVector<const NamedDecl *, 2>>
      CopyOf;
  llvm::SmallPtrSet<const DeclRefExpr *, 16> Callees;
  std::vector<const DeclRefExpr *> FunctionRefs;
  std::vector<const Expr *> Tested;

  auto SourceOf = [&](const Expr *E) -> const NamedDecl * {
    E = E->IgnoreParenImpCasts();
    while (const auto *CE = dyn_cast<ExplicitCastExpr>(E)) {
      if (isa<CXXDynamicCastExpr>(CE))
        return nullptr;
      E = CE->getSubExpr()->IgnoreParenImpCasts();
    }
    if (const auto *ME = dyn_cast<MemberExpr>(E))
      return dyn_cast<FieldDecl>(ME->getMemberDecl());
    const auto *DRE = dyn_cast<DeclRefExpr>(E);
    const auto *PVD = DRE ? dyn_cast<ParmVarDecl>(DRE->getDecl()) : nullptr;
    if (!PVD || !Func || PVD->getDeclContext() != Func)
      return nullptr;
    return PVD;
  };
  auto NoteCopy = [&](const Decl *D, const Expr *From) {
    const auto *VD = dyn_cast_or_null<VarDecl>(D);
    if (!VD || isa<ParmVarDecl>(VD) || !VD->getType()->isPointerType() || !From)
      return;
    if (const NamedDecl *Source = SourceOf(From))
      CopyOf[VD].push_back(Source);
  };

  auto NoteTest = [&](const Expr *P, SourceLocation Loc) {
    if (P && P->getType()->isPointerType() && !isInAssertMacro(Loc, Ctx))
      Tested.push_back(P->IgnoreParenImpCasts());
  };

  findMatchesIn(Contributor, [&](const DynTypedNode &Node) {
    if (const auto *VD = Node.get<VarDecl>()) {
      NoteCopy(VD, VD->getInit());
      return;
    }
    if (const auto *S = Node.get<Stmt>()) {
      const Expr *Cond = nullptr;
      if (const auto *If = dyn_cast<IfStmt>(S))
        Cond = If->getCond();
      else if (const auto *While = dyn_cast<WhileStmt>(S))
        Cond = While->getCond();
      else if (const auto *Do = dyn_cast<DoStmt>(S))
        Cond = Do->getCond();
      else if (const auto *For = dyn_cast<ForStmt>(S))
        Cond = For->getCond();
      else if (const auto *CO = dyn_cast<AbstractConditionalOperator>(S))
        Cond = CO->getCond();
      if (Cond)
        NoteTest(Cond->IgnoreParenImpCasts(), Cond->getBeginLoc());
      if (const auto *BO = dyn_cast<BinaryOperator>(S); BO && BO->isLogicalOp())
        for (const Expr *Op : {BO->getLHS(), BO->getRHS()})
          NoteTest(Op->IgnoreParenImpCasts(), Op->getBeginLoc());
    }
    const auto *E = Node.get<Expr>();
    if (!E)
      return;
    if (const auto *CE = dyn_cast<CallExpr>(E)) {
      if (const auto *DRE =
              dyn_cast<DeclRefExpr>(CE->getCallee()->IgnoreParenImpCasts()))
        Callees.insert(DRE);
    } else if (const auto *DRE = dyn_cast<DeclRefExpr>(E)) {
      if (isa<FunctionDecl>(DRE->getDecl()))
        FunctionRefs.push_back(DRE);
    } else if (const auto *BO = dyn_cast<BinaryOperator>(E);
               BO && BO->getOpcode() == BO_Assign) {
      if (const auto *LHS =
              dyn_cast<DeclRefExpr>(BO->getLHS()->IgnoreParenImpCasts()))
        NoteCopy(LHS->getDecl(), BO->getRHS());
    }
    NoteTest(nullTestedPointer(E, Ctx), E->getBeginLoc());
  });

  for (const DeclRefExpr *DRE : FunctionRefs) {
    if (Callees.contains(DRE))
      continue;
    const auto *FD = cast<FunctionDecl>(DRE->getDecl());
    for (const ParmVarDecl *PVD : FD->parameters())
      if (PVD->getType()->isPointerType())
        MaybeNull.push_back(createDeclPointerLevel(PVD));
  }

  for (const Expr *P : Tested) {
    if (const NamedDecl *Source = SourceOf(P)) {
      MaybeNull.push_back(createDeclPointerLevel(Source));
      continue;
    }
    if (const auto *DRE = dyn_cast<DeclRefExpr>(P))
      if (const auto *VD = dyn_cast<VarDecl>(DRE->getDecl()))
        for (const NamedDecl *Source : CopyOf.lookup(VD))
          MaybeNull.push_back(createDeclPointerLevel(Source));
  }
}

class EvidenceCollector final : public NullabilitySafetyHandler {
public:
  llvm::DenseMap<const Decl *, DeclEvidence> ByContributor;

  void startFunction(const Decl *Def) override {
    Contributor = contributorOf(Def);
  }

  void finishFunction(const Decl *) override { Contributor = nullptr; }

  void handleNullableDereference(const Expr *, QualType) override {}

  void handleMemberAssignEvidence(const Expr *, const FieldDecl *Member,
                                  NullabilityEvidence Kind) override {
    record(createDeclPointerLevel(Member), Kind);
  }

  void handleReturnEvidence(const Expr *, const FunctionDecl *Func,
                            NullabilityEvidence Kind) override {
    record(createDeclPointerLevel(Func, /*IsFunRet=*/true), Kind);
  }

  void handleParameterEvidence(const Expr *, const ParmVarDecl *Param,
                               const FunctionDecl *,
                               NullabilityEvidence Kind) override {
    record(createDeclPointerLevel(Param), Kind);
  }

  void handleAllReturnsNonnull(const FunctionDecl *Func) override {
    if (Contributor)
      ByContributor[Contributor].AllReturnsNonnull.push_back(
          createDeclPointerLevel(Func, /*IsFunRet=*/true));
  }

private:
  const Decl *Contributor = nullptr;

  void record(DeclPointerLevel DPL, NullabilityEvidence Kind) {
    if (!Contributor)
      return;
    DeclEvidence &E = ByContributor[Contributor];
    switch (Kind) {
    case NullabilityEvidence::Nonnull:
      E.Nonnull.push_back(DPL);
      break;
    case NullabilityEvidence::Nullable:
      E.Nullable.push_back(DPL);
      break;
    case NullabilityEvidence::MaybeNull:
      E.MaybeNull.push_back(DPL);
      break;
    }
  }
};
} // namespace

namespace clang::ssaf {
class NullabilitySafetyTUSummaryExtractor : public TUSummaryExtractor {
public:
  NullabilitySafetyTUSummaryExtractor(TUSummaryBuilder &Builder)
      : TUSummaryExtractor(Builder) {}

  void HandleTranslationUnit(ASTContext &Ctx) override;

private:
  EntityPointerLevelSet translate(const std::vector<DeclPointerLevel> &DPLs,
                                  ASTContext &Ctx);
  std::unique_ptr<NullabilitySafetyEntitySummary>
  summarize(const DeclEvidence &E, ASTContext &Ctx);
};
} // namespace clang::ssaf

EntityPointerLevelSet NullabilitySafetyTUSummaryExtractor::translate(
    const std::vector<DeclPointerLevel> &DPLs, ASTContext &Ctx) {
  EntityPointerLevelSet Result;
  for (const DeclPointerLevel &DPL : DPLs) {
    Expected<EntityPointerLevel> EPL = toEntityPointerLevel(DPL, Ctx, *this);
    if (EPL)
      Result.insert(*EPL);
    else
      logWarningFromError(EPL.takeError());
  }
  return Result;
}

std::unique_ptr<NullabilitySafetyEntitySummary>
NullabilitySafetyTUSummaryExtractor::summarize(const DeclEvidence &E,
                                               ASTContext &Ctx) {
  return std::make_unique<NullabilitySafetyEntitySummary>(
      translate(E.Nonnull, Ctx), translate(E.Nullable, Ctx),
      translate(E.AllReturnsNonnull, Ctx), translate(E.MaybeNull, Ctx));
}

void NullabilitySafetyTUSummaryExtractor::HandleTranslationUnit(
    ASTContext &Ctx) {
  bool SkipSystemHeaders = !getOptions().ExtractFromSystemHeaders;
  const SourceManager &SM = Ctx.getSourceManager();

  EvidenceCollector Collector;
  runNullabilitySafetyOnTU(
      Ctx.getTranslationUnitDecl(), Collector,
      NullabilitySafetyOptions::fromLangOptions(Ctx.getLangOpts()),
      [&](const Decl *Def) {
        return !SkipSystemHeaders || !SM.isInSystemHeader(Def->getLocation());
      });

  extractAndAddSummaries(
      *this, SummaryBuilder, Ctx,
      [&](const std::vector<const NamedDecl *> &Decls) {
        DeclEvidence E;
        auto It = Collector.ByContributor.find(Decls[0]->getCanonicalDecl());
        if (It != Collector.ByContributor.end())
          E = It->second;
        for (const NamedDecl *D : Decls)
          collectVetoes(D, Ctx, E.MaybeNull);
        return summarize(E, Ctx);
      },
      NullabilitySafetyEntitySummary::Name);
}

namespace clang::ssaf {
// NOLINTNEXTLINE(misc-use-internal-linkage)
volatile int NullabilitySafetyExtractorAnchorSource = 0;
} // namespace clang::ssaf

static clang::ssaf::TUSummaryExtractorRegistry::Add<
    NullabilitySafetyTUSummaryExtractor>
    RegisterExtractor(NullabilitySafetyEntitySummary::Name,
                      "Extract nullability evidence");
