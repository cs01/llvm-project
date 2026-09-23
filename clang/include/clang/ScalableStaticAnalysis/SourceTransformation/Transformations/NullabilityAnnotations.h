//===- NullabilityAnnotations.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The nullability-annotations transformation writes the whole-program
// nullability inference (NullabilityInferenceAnalysisResult) back into the
// source: it inserts _Nonnull or _Nullable on the outermost pointer of each
// inferred parameter, field and function return. Inferred declarators that
// cannot be annotated, and declarators a nullable value may reach through
// pointer flow without being inferred _Nullable, are reported instead.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SCALABLESTATICANALYSIS_SOURCETRANSFORMATION_TRANSFORMATIONS_NULLABILITYANNOTATIONS_H
#define LLVM_CLANG_SCALABLESTATICANALYSIS_SOURCETRANSFORMATION_TRANSFORMATIONS_NULLABILITYANNOTATIONS_H

#include "clang/ScalableStaticAnalysis/SourceTransformation/Transformation.h"
#include "llvm/ADT/StringRef.h"

namespace clang::ssaf {

enum class NullabilitySkipReason {
  EmissionFailed,
  InnerPointerLevel,
  MacroExpansion,
  NoPointerSpelling,
};

llvm::StringRef messageFor(NullabilitySkipReason Reason);

class NullabilityAnnotations final : public Transformation {
public:
  using Transformation::Transformation;

  void HandleTranslationUnit(clang::ASTContext &Ctx) override;
};

} // namespace clang::ssaf

#endif // LLVM_CLANG_SCALABLESTATICANALYSIS_SOURCETRANSFORMATION_TRANSFORMATIONS_NULLABILITYANNOTATIONS_H
