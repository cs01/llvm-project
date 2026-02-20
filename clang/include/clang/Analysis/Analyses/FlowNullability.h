//=- FlowNullability.h - Flow-sensitive null dereference checking -*- C++ -*-=//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines APIs for invoking flow-sensitive nullability analysis
// that detects dereferences of nullable pointers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_FLOWNULLABILITY_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_FLOWNULLABILITY_H

namespace clang {

class AnalysisDeclContext;
class Expr;
class QualType;

class FlowNullabilityHandler {
public:
  virtual ~FlowNullabilityHandler();
  virtual void handleNullableDereference(const Expr *DerefExpr,
                                         QualType PtrType) = 0;
};

void runFlowNullabilityAnalysis(AnalysisDeclContext &AC,
                                FlowNullabilityHandler &Handler,
                                bool StrictMode);

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_FLOWNULLABILITY_H
