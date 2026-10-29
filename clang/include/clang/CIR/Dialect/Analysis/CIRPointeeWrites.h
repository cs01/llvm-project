//===- CIRPointeeWrites.h - Is a pointer parameter written through -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each pointer parameter, whether the memory it points to may be written
// through it: directly, through an address derived from it (a member, an
// element, a base or derived class, a cast), or by a callee it is passed to.
// A parameter that is never written through can point to const.
//
// CIR does not keep const qualifiers, so whether a callee's argument is
// read-only is asked of the caller, which has the AST.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_ANALYSIS_CIRPOINTEEWRITES_H
#define CLANG_CIR_DIALECT_ANALYSIS_CIRPOINTEEWRITES_H

#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "mlir/IR/BuiltinOps.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace cir {

enum class PointerArgUse {
  /// The callee only reads through this argument.
  ReadOnly,
  /// The callee may write through it, or nothing is known about it.
  Writes,
  /// The callee's parameter is a pointer to non-const that could itself
  /// become a pointer to const; the caller's fact then depends on it.
  Forward,
};

struct PointeeWriteFact {
  std::string function;
  /// The CIR argument index, which counts `this` for a method.
  unsigned index = 0;
  bool written = false;
  /// What writes through it or lets it escape, when written.
  std::string why;
  /// Callee parameters it is passed to under PointerArgUse::Forward, as
  /// (function, CIR argument index).
  llvm::SmallVector<std::pair<std::string, unsigned>> forwards;
};

void collectPointeeWrites(
    mlir::ModuleOp module,
    llvm::function_ref<PointerArgUse(cir::FuncOp callee, unsigned index)>
        argUse,
    llvm::function_ref<void(const PointeeWriteFact &)> emit);

} // namespace cir

#endif // CLANG_CIR_DIALECT_ANALYSIS_CIRPOINTEEWRITES_H
