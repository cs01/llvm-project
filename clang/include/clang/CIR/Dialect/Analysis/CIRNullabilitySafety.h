//===- CIRNullabilitySafety.h - Flow-sensitive nullability on CIR -*- C++
//-*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Flow-sensitive nullability analysis on ClangIR. Source nullability comes
// from the `cir.nullability` attributes CIRGen attaches to function arguments
// and results, allocas, loads and calls; functions are checked when CIRGen
// marked them with `cir.nullability_safety`. The analysis is a dense forward
// dataflow analysis over the structured CIR that CIRGen emits, tracking
// non-escaping local slots and member paths rooted at them, and narrowing on
// the conditions of `cir.if`, `cir.ternary` and loop `cir.condition` edges.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_ANALYSIS_CIRNULLABILITYSAFETY_H
#define CLANG_CIR_DIALECT_ANALYSIS_CIRNULLABILITYSAFETY_H

#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include <string>

namespace cir {

enum class NullabilityFindingKind {
  Dereference,
  Call,
  Return,
  Assignment,
  Argument,
  Arithmetic,
};

struct NullabilityFinding {
  NullabilityFindingKind kind;
  mlir::Location loc;
  std::string subject;
  unsigned argIndex = 0;
};

struct NullabilitySafetyCIROptions {
  bool unspecifiedIsNullable = false;
};

void runNullabilitySafety(
    mlir::ModuleOp module, const NullabilitySafetyCIROptions &options,
    llvm::function_ref<void(const NullabilityFinding &)> report);

enum class NullabilityFactKind { Param, Return, Field };

enum class InferredNullability { Unknown, NonNull, Nullable };

// Evidence about how a pointer is used, gathered for every function rather
// than only opted-in ones, so unannotated code can be described. `inferred`
// prefers explicit annotations, then any evidence that null reaches the
// pointer, then an unchecked dereference. `mustDeref` is stronger than
// `derefUnchecked`: the parameter as passed is dereferenced before every way
// out of the function (return, throw, noreturn call, trap, or any call that
// might not return), so a null argument is already undefined behavior.
struct NullabilityFact {
  NullabilityFact(NullabilityFactKind kind, mlir::Location loc)
      : kind(kind), loc(loc) {}

  NullabilityFactKind kind;
  std::string function;
  std::string record;
  std::string name;
  unsigned index = 0;
  mlir::Location loc;
  std::string declared;
  bool nullChecked = false;
  bool assertedNonNull = false;
  bool derefUnchecked = false;
  bool forwardedToNonNull = false;
  bool assignedNull = false;
  bool callersPassNull = false;
  bool allPathsNonNull = false;
  bool mustDeref = false;
  bool reassigned = false;
  bool addressTaken = false;
  bool arithmetic = false;
  InferredNullability inferred = InferredNullability::Unknown;
};

void collectNullabilityFacts(
    mlir::ModuleOp module, const NullabilitySafetyCIROptions &options,
    llvm::function_ref<bool(mlir::Location)> include,
    llvm::function_ref<void(const NullabilityFact &)> emit);

} // namespace cir

#endif // CLANG_CIR_DIALECT_ANALYSIS_CIRNULLABILITYSAFETY_H
