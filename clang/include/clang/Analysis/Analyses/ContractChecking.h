//===- ContractChecking.h - Compile-time contract checking ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file
/// A CFG dataflow pass that checks a callee's preconditions at each call site.
///
/// The pass is unsound and incomplete by construction, and deliberately so: it
/// reports only preconditions it can show are *violated*, never ones it merely
/// cannot prove. Warning on everything unproven is what makes this class of
/// analysis unusable on real C, so "cannot tell" is silence here.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_CONTRACTCHECKING_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_CONTRACTCHECKING_H

namespace clang {

class AnalysisDeclContext;
class CallExpr;
class ContractClause;
class FunctionDecl;

namespace contracts {

/// Receives the violations the pass finds.
class ContractViolationReporter {
public:
  virtual ~ContractViolationReporter() = default;

  /// \p Call passes an argument that violates \p Clause, a precondition of
  /// \p Callee.
  virtual void reportPreconditionViolated(const CallExpr *Call,
                                          const FunctionDecl *Callee,
                                          const ContractClause &Clause) = 0;
};

/// Runs precondition checking over the body in \p AC.
void runContractChecking(AnalysisDeclContext &AC,
                         ContractViolationReporter &Reporter);

} // namespace contracts
} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_CONTRACTCHECKING_H
