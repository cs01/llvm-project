//===- CIRReturnOrigins.h - Which arguments a returned pointer comes from -===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// For each function returning a pointer (a C++ reference is one in CIR) or a
// view object such as llvm::StringRef, the pointer arguments the returned
// value may be derived from by address arithmetic alone: the argument itself,
// a member, an element, a base or derived class, a cast. A returned object
// built in the return slot by a constructor derives from the constructor
// arguments the caller says carry their origin. Such an argument can be
// [[clang::lifetimebound]]: the result refers to what it refers to.
//
// A pointer loaded from memory, returned by a call or made by an op this
// file does not know is not traced further. That only loses origins: an
// argument that is reported is one a returned value can come from.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_DIALECT_ANALYSIS_CIRRETURNORIGINS_H
#define CLANG_CIR_DIALECT_ANALYSIS_CIRRETURNORIGINS_H

#include "mlir/IR/BuiltinOps.h"
#include "clang/CIR/Dialect/IR/CIRDialect.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <string>

namespace cir {

struct ReturnOriginFact {
  std::string function;
  /// CIR argument indices, which count `this` for a method.
  llvm::SmallVector<unsigned> args;
};

/// carries(ctor, index): whether the constructor's CIR argument index (past
/// `this`) makes the constructed object refer to what that argument refers
/// to. Only the AST knows, from lifetimebound and gsl attributes.
void collectReturnOrigins(
    mlir::ModuleOp module,
    llvm::function_ref<bool(cir::FuncOp ctor, unsigned index)> carries,
    llvm::function_ref<void(const ReturnOriginFact &)> emit);

} // namespace cir

#endif // CLANG_CIR_DIALECT_ANALYSIS_CIRRETURNORIGINS_H
