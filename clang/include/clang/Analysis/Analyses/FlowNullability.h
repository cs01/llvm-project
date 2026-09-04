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
class Expr;
class FieldDecl;
class FunctionDecl;
class ParmVarDecl;
class VarDecl;

/// Receives the diagnostics and evidence produced by
/// runFlowNullabilityAnalysis. Only handleNullableDereference is required;
/// the rest default to no-ops.
class FlowNullabilityHandler {
public:
  virtual ~FlowNullabilityHandler();
  /// A pointer that may be null is dereferenced (*p, p->f, p[i], or a smart
  /// pointer's operator* / operator->).
  virtual void handleNullableDereference(const Expr *DerefExpr,
                                         QualType PtrType) = 0;
  /// A pointer that may be null is used in arithmetic (p + i, p++, p += i).
  virtual void handleNullableArithmetic(const Expr *ArithExpr,
                                        QualType PtrType) {}
  /// A function with a _Nonnull return type returns a value that may be null.
  virtual void handleNullableReturn(const Expr *ReturnExpr, QualType ExprType,
                                    QualType ReturnType) {}
  /// A _Nonnull variable is initialized or assigned a value that may be null.
  virtual void handleNullableAssignment(const Expr *AssignExpr,
                                        const VarDecl *LHSVar) {}
  /// A _Nonnull field is assigned or aggregate-initialized with a value that
  /// may be null.
  virtual void handleNullableMemberAssignment(const Expr *AssignExpr,
                                              const FieldDecl *Member) {}
  /// A value that may be null is passed to a _Nonnull (or
  /// __attribute__((nonnull))) parameter.
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

  /// Evidence collection: called when a pointer argument is passed to a
  /// function parameter. \p IsNonnull is true if the argument is provably
  /// non-null at the call site.
  virtual void handleParameterEvidence(const Expr *ArgExpr,
                                       const ParmVarDecl *Param,
                                       const FunctionDecl *Func,
                                       bool IsNonnull) {}

  /// Summary evidence: called after the dataflow fixpoint when every
  /// return path in the function returns a provably non-null expression
  /// (address-of, this, new, narrowed var, etc.). Enables callers to
  /// treat the function's return as implicitly _Nonnull.
  virtual void handleAllReturnsNonnull(const FunctionDecl *Func) {}

  /// Query: has this function been previously analyzed and found to have
  /// all-returns-nonnull? Used by callers within the same TU to narrow
  /// returned pointers. Returns false by default (conservative).
  virtual bool isKnownAllReturnsNonnull(const FunctionDecl *Func) const {
    return false;
  }
};

/// Run the flow-sensitive nullability analysis over the CFG of the function
/// in \p AC, reporting through \p Handler. \p DefaultNullability is how an
/// unannotated (_Null_unspecified) pointer is treated; \p StdlibAnnotations
/// enables the built-in list of C library functions that return null on
/// failure (malloc, fopen, ...).
void runFlowNullabilityAnalysis(AnalysisDeclContext &AC,
                                FlowNullabilityHandler &Handler,
                                NullabilityKind DefaultNullability,
                                bool StdlibAnnotations = true);

} // namespace clang

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_FLOWNULLABILITY_H
