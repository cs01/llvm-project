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
class DiagnosticsEngine;
/// Makes the safety diagnostics errors -w and -Wno-* cannot turn off.
void safeCMarkSafetyDiagsUnsilenceable(DiagnosticsEngine &Diags);

class ASTContext;
class CallExpr;
class QualType;

/// The Safe C kind of a pointer type (from its [[clang::annotate_type]]
/// sugar): "owned", "static", "unchecked"..., or "" for a plain borrow. Arrays
/// give their elements' kind.
llvm::StringRef safeCPointerKind(QualType T);
class Decl;
class Sema;
class Stmt;

/// Sema for __builtin_safec_take(lvalue): moves an owned pointer out of a
/// field or global, leaving null (and a zero count). Returns true on error.
bool checkSafeCTakeBuiltin(Sema &S, CallExpr *TheCall);

/// A library function with no safe use: an error that says what to use.
class FunctionDecl;
bool checkSafeCUnsafeFunction(Sema &S, const FunctionDecl *F, SourceLocation Loc);

/// What a function pointer promises must be what the function promises.
void checkSafeCFunctionConversion(Sema &S, QualType Dst, const Expr *Src);
/// The fields of a struct or union.
void checkSafeCPointerStorage(Sema &S, Decl *D);

/// Every expression of a function body or a global's initializer.
void checkSafeCPointerKindsIn(Sema &S, Stmt *Body);
void checkSafeCPointerKindsIn(Sema &S, Decl *GlobalVar);

/// A member of a tagged union is read or written only where the
/// tag is known to name it (after `if (s->kind == V)`, in `case V:`, or after
/// `s->kind = V`).
void checkSafeCTaggedAccess(Sema &S, Decl *Fn, Stmt *Body);
/// Kinds of declarations and types without rules (end of translation unit).
void checkSafeCDeclarations(Sema &S);

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

  /// Calls already reported as having no safe use.
  llvm::SmallVector<SourceLocation, 4> UnsafeCalls;
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
