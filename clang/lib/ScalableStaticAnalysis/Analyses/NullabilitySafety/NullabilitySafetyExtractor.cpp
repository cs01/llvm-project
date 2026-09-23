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
#include "clang/Analysis/Analyses/NullabilitySafety.h"
#include "clang/Basic/SourceManager.h"
#include "clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevel.h"
#include "clang/ScalableStaticAnalysis/Analyses/NullabilitySafety/NullabilitySafety.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/ExtractorRegistry.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryBuilder.h"
#include "clang/ScalableStaticAnalysis/Core/TUSummary/TUSummaryExtractor.h"
#include "llvm/ADT/DenseMap.h"
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
        auto It = Collector.ByContributor.find(Decls[0]->getCanonicalDecl());
        if (It == Collector.ByContributor.end())
          return summarize(DeclEvidence(), Ctx);
        return summarize(It->second, Ctx);
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
