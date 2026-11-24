//===--- SafeCState.h - Safe C (-fsafe-c) per-TU state ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// State Safe C keeps for a translation unit. Sema holds it through a pointer,
// so Safe C can grow without editing Sema.h (which nearly every file in Clang
// includes, so each edit there rebuilds most of the compiler).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_SEMA_SAFECSTATE_H
#define LLVM_CLANG_LIB_SEMA_SAFECSTATE_H

#include "clang/Basic/SourceLocation.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

class CallExpr;
class Decl;
class Sema;
class Stmt;

/// Whether \p D takes ownership of (frees) parameter \p Idx (0-based), as
/// free and realloc do, or as ownership_takes says.
bool safeCTakesParam(const Decl *D, unsigned Idx);

/// Sema for __builtin_safec_take(lvalue): moves an owned pointer out of a
/// field or global, leaving null (and a zero count). Returns true on error.
bool checkSafeCTakeBuiltin(Sema &S, CallExpr *TheCall);

/// M2 ownership checks, staged behind -mllvm -safe-c-ownership until the
/// ports are annotated for them.
bool safeCOwnershipEnabled(const Sema &S);

/// Rule 10: a struct field or global holds no borrow. Its pointers are
/// __safec_owned, __safec_static or __safec_unchecked, unless the struct is a
/// __safec_view (which lives only in locals and parameters).
void checkSafeCPointerStorage(Sema &S, Decl *D);

/// Rules 11 and 12 and the static kind, per expression: a __safec_static
/// holds only static addresses, an __safec_unchecked is read only in
/// __unsafe (or copied to another unchecked), a view is written only by name.
/// Run on a function body or a global's initializer.
void checkSafeCPointerKindsIn(Sema &S, Stmt *Body);
void checkSafeCPointerKindsIn(Sema &S, Decl *GlobalVar);

class SafeCState {
public:
  /// An `__unsafe` region (a statement or function carrying
  /// [[clang::suppress("safe-c")]]) and whether a safety diagnostic was
  /// suppressed in it; an unused one is reported at the end of the TU.
  struct Region {
    SourceRange Range;
    bool IsFunction = false;
    bool Used = false;
  };

  /// Regions being parsed: diagnostics raised now are inside them.
  unsigned Depth = 0;
  /// Every region seen, for diagnostics raised after parsing (end-of-function
  /// and end-of-TU analyses).
  llvm::SmallVector<Region, 4> Regions;
  /// Indices into Regions of the regions being parsed, innermost last.
  llvm::SmallVector<unsigned, 4> Open;
};

} // namespace clang

#endif // LLVM_CLANG_LIB_SEMA_SAFECSTATE_H
