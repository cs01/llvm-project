//===--- CIRModernize.h - CIR-proven idiomatic C++ rewrites -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Deterministic source rewrites whose safety is decided on ClangIR and whose
// edits are built on the Clang AST. The first rewrite turns a pointer
// parameter into a reference when CIR proves it is dereferenced before every
// way out of the function, so a null argument was already undefined behavior.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZE_H
#define CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZE_H

#include "mlir/IR/BuiltinOps.h"

namespace clang {
class ASTContext;
class DiagnosticsEngine;

struct CIRModernizeOptions {
  bool AllowPublic = false;
  bool Apply = false;
};

void runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                     DiagnosticsEngine &Diags,
                     const CIRModernizeOptions &Options);

} // namespace clang

#endif // CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZE_H
