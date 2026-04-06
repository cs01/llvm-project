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

#include "clang/AST/Type.h"
#include "clang/Basic/Specifiers.h"

namespace clang {

class AnalysisDeclContext;
class CXXRecordDecl;
class Expr;
class FieldDecl;
class FunctionDecl;
class ParmVarDecl;
class VarDecl;

class FlowNullabilityHandler {
public:
  virtual ~FlowNullabilityHandler();
  virtual void handleNullableDereference(const Expr *DerefExpr,
                                         QualType PtrType) = 0;
  virtual void handleNullableArithmetic(const Expr *ArithExpr,
                                        QualType PtrType) {}
  virtual void handleNullableReturn(const Expr *ReturnExpr, QualType ExprType,
                                    QualType ReturnType) {}
  virtual void handleNullableAssignment(const Expr *AssignExpr,
                                        const VarDecl *LHSVar) {}
  virtual void handleNullableArgument(const Expr *ArgExpr,
                                      const ParmVarDecl *Param) {}

  /// Evidence collection: called when a pointer member is assigned.
  /// \p IsNonnull is true if the RHS is provably non-null.
  virtual void handleMemberAssignEvidence(const Expr *AssignExpr,
                                          const FieldDecl *Member,
                                          bool IsNonnull) {}

  /// Evidence collection: called when a function returns a pointer.
  /// \p IsNonnull is true if the returned expression is provably non-null.
  virtual void handleReturnEvidence(const Expr *RetExpr,
                                    const FunctionDecl *Func, bool IsNonnull) {}
};

void runFlowNullabilityAnalysis(AnalysisDeclContext &AC,
                                FlowNullabilityHandler &Handler,
                                bool StrictMode,
                                NullabilityKind DefaultNullability);

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_FLOWNULLABILITY_H
