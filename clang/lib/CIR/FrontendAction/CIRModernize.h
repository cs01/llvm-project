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
#include <string>

namespace clang {
class ASTContext;
class DiagnosticsEngine;

struct CIRModernizeOptions {
  bool AllowPublic = false;
  bool Apply = false;
  /// Project mode: write candidates, edits and blockers here instead of
  /// reporting. Signatures declared in headers may change, because the driver
  /// that merges every translation unit's export sees every caller.
  std::string ExportPath;
  /// Project mode: functions, by USR and parameter index, that another
  /// translation unit proposed. This one reports the edits its own
  /// declarations and calls need, or why it blocks them.
  std::string PlanPath;
  /// Comma-separated rewrites to make (ref, const); empty means all.
  std::string Only;
};

void runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                     DiagnosticsEngine &Diags,
                     const CIRModernizeOptions &Options);

} // namespace clang

#endif // CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZE_H
