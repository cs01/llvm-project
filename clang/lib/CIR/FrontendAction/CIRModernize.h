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
#include "clang/Lex/PPCallbacks.h"
#include "llvm/ADT/StringSet.h"
#include <string>
#include <vector>

namespace clang {
class ASTContext;
class DiagnosticsEngine;
class Preprocessor;

/// The preprocessor conditionals of a translation unit. CIR facts and AST
/// edits describe one build configuration; code inside #if may be different
/// in another, and a branch this build skips may call a rewritten function
/// in a way no edit here fixes.
class ConditionalRecord : public PPCallbacks {
public:
  explicit ConditionalRecord(Preprocessor &PP) : PP(PP) {}

  void If(SourceLocation Loc, SourceRange, ConditionValueKind) override {
    Open.push_back(Loc);
  }
  void Ifdef(SourceLocation Loc, const Token &,
             const MacroDefinition &) override {
    Open.push_back(Loc);
  }
  void Ifndef(SourceLocation Loc, const Token &,
              const MacroDefinition &) override {
    Open.push_back(Loc);
  }
  void Endif(SourceLocation Loc, SourceLocation IfLoc) override;
  void SourceRangeSkipped(SourceRange Range, SourceLocation EndifLoc) override;

  /// Whether the file range R overlaps an #if region other than an include
  /// guard.
  bool overlaps(SourceRange R) const;
  /// Identifiers in the branches this build skipped, outside system headers.
  const llvm::StringSet<> &skippedNames() const { return Skipped; }
  bool skippedAny() const { return !Skipped.empty(); }

private:
  Preprocessor &PP;
  std::vector<SourceLocation> Open;
  std::vector<SourceRange> Regions;
  llvm::StringSet<> Skipped;
};

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
  /// Comma-separated rewrites or groups to make; empty means the default ones.
  std::string Only;
  /// Comma-separated rewrites or groups not to make.
  std::string Skip;
  /// Owned by the preprocessor; null if it was not installed.
  const ConditionalRecord *Conditionals = nullptr;
};

/// Module may be null when CIRGen failed: then only planned candidates, which
/// need no facts from this translation unit, are considered.
void runCIRModernize(mlir::ModuleOp Module, ASTContext &C,
                     DiagnosticsEngine &Diags,
                     const CIRModernizeOptions &Options);

} // namespace clang

#endif // CLANG_LIB_CIR_FRONTENDACTION_CIRMODERNIZE_H
