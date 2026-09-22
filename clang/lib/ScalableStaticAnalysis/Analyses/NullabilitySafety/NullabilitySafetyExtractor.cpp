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
};

class EvidenceCollector final : public NullabilitySafetyHandler {
public:
  llvm::DenseMap<const Decl *, DeclEvidence> ByContributor;
  const Decl *Contributor = nullptr;

  void handleNullableDereference(const Expr *, QualType) override {}

  void handleMemberAssignEvidence(const Expr *, const FieldDecl *Member,
                                  bool IsNonnull) override {
    record(createDeclPointerLevel(Member), IsNonnull);
  }

  void handleReturnEvidence(const Expr *, const FunctionDecl *Func,
                            bool IsNonnull) override {
    record(createDeclPointerLevel(Func, /*IsFunRet=*/true), IsNonnull);
  }

  void handleParameterEvidence(const Expr *, const ParmVarDecl *Param,
                               const FunctionDecl *, bool IsNonnull) override {
    record(createDeclPointerLevel(Param), IsNonnull);
  }

  void handleAllReturnsNonnull(const FunctionDecl *Func) override {
    if (Contributor)
      ByContributor[Contributor].AllReturnsNonnull.push_back(
          createDeclPointerLevel(Func, /*IsFunRet=*/true));
  }

private:
  void record(DeclPointerLevel DPL, bool IsNonnull) {
    if (!Contributor)
      return;
    DeclEvidence &E = ByContributor[Contributor];
    (IsNonnull ? E.Nonnull : E.Nullable).push_back(DPL);
  }
};

const Decl *contributorOf(const Decl *Def) {
  const Decl *D = isa<BlockDecl>(Def) ? Def->getNonClosureContext() : Def;
  return D ? D->getCanonicalDecl() : nullptr;
}
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

void NullabilitySafetyTUSummaryExtractor::HandleTranslationUnit(
    ASTContext &Ctx) {
  const LangOptions &LO = Ctx.getLangOpts();
  NullabilitySafetyOptions Options;
  Options.DefaultNullability = LO.getNullabilityDefault();
  Options.LibcNullableReturns = LO.NullabilityLibcNullableReturns;
  bool SkipSystemHeaders = !getOptions().ExtractFromSystemHeaders;
  const SourceManager &SM = Ctx.getSourceManager();

  EvidenceCollector Collector;
  runNullabilitySafetyOnTU(
      Ctx.getTranslationUnitDecl(), Collector, Options,
      [&](const Decl *Def) {
        Collector.Contributor = nullptr;
        if (SkipSystemHeaders && SM.isInSystemHeader(Def->getLocation()))
          return false;
        if (Options.DefaultNullability == NullabilityKind::Unspecified &&
            !hasExplicitNullabilityAnnotations(Def))
          return false;
        Collector.Contributor = contributorOf(Def);
        return true;
      },
      [] {});

  extractAndAddSummaries(
      *this, SummaryBuilder, Ctx,
      [&](const std::vector<const NamedDecl *> &Decls) {
        EntityPointerLevelSet Nonnull, Nullable, AllReturnsNonnull;
        auto It = Collector.ByContributor.find(Decls[0]->getCanonicalDecl());
        if (It != Collector.ByContributor.end()) {
          Nonnull = translate(It->second.Nonnull, Ctx);
          Nullable = translate(It->second.Nullable, Ctx);
          AllReturnsNonnull = translate(It->second.AllReturnsNonnull, Ctx);
        }
        return std::make_unique<NullabilitySafetyEntitySummary>(
            std::move(Nonnull), std::move(Nullable),
            std::move(AllReturnsNonnull));
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
