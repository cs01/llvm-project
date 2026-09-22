//===- NullabilitySafetyAnalysis.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Whole-program nullability evidence and inference.
//
// NullabilityEvidenceAnalysisResult is the union of every contributor's
// NullabilitySafety summary.
//
// NullabilityInferenceAnalysisResult combines it with the pointer-flow graph.
// Pointer-flow edges map an assignee to the values assigned to it; a nullable
// value makes its assignee nullable, so nullable evidence is propagated along
// the edges in reverse. The graph is flow-insensitive (it does not see null
// checks), so propagation only vetoes: Nullable is the direct nullable
// evidence, Nonnull is the nonnull evidence that no nullable value reaches,
// and NullableReachable is what propagation reached, for reporting only.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SCALABLESTATICANALYSIS_ANALYSES_NULLABILITYSAFETY_NULLABILITYSAFETYANALYSIS_H
#define LLVM_CLANG_SCALABLESTATICANALYSIS_ANALYSES_NULLABILITYSAFETY_NULLABILITYSAFETYANALYSIS_H

#include "clang/ScalableStaticAnalysis/Analyses/EntityPointerLevel/EntityPointerLevel.h"
#include "clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisName.h"
#include "clang/ScalableStaticAnalysis/Core/WholeProgramAnalysis/AnalysisResult.h"
#include "llvm/ADT/StringRef.h"

namespace clang::ssaf {

constexpr llvm::StringLiteral NullabilityEvidenceAnalysisResultName =
    "NullabilityEvidenceAnalysisResult";
constexpr llvm::StringLiteral NullabilityInferenceAnalysisResultName =
    "NullabilityInferenceAnalysisResult";

struct NullabilityEvidenceAnalysisResult final : AnalysisResult {
  static AnalysisName analysisName() {
    return AnalysisName(NullabilityEvidenceAnalysisResultName.str());
  }

  EntityPointerLevelSet NonnullEvidence;
  EntityPointerLevelSet NullableEvidence;
  EntityPointerLevelSet AllReturnsNonnull;
};

struct NullabilityInferenceAnalysisResult final : AnalysisResult {
  static AnalysisName analysisName() {
    return AnalysisName(NullabilityInferenceAnalysisResultName.str());
  }

  EntityPointerLevelSet Nonnull;
  EntityPointerLevelSet Nullable;
  EntityPointerLevelSet NullableReachable;
};

} // namespace clang::ssaf

#endif // LLVM_CLANG_SCALABLESTATICANALYSIS_ANALYSES_NULLABILITYSAFETY_NULLABILITYSAFETYANALYSIS_H
