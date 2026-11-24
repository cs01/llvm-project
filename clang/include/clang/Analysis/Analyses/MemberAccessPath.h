//===- MemberAccessPath.h - A variable and the fields below it --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A place named by a member access chain, `v.a.b` or `p->a->b`: the root
// variable and the fields on the way. Shared by the flow-sensitive checks
// that narrow what they know about such places (nullability, Safe C union
// tags), so both see the same paths.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_ANALYSIS_ANALYSES_MEMBERACCESSPATH_H
#define LLVM_CLANG_ANALYSIS_ANALYSES_MEMBERACCESSPATH_H

#include "clang/AST/Decl.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/SmallVector.h"
#include <optional>

namespace clang {
class Expr;

struct MemberAccessPath {
  /// The variable the chain starts from; null for `this->` paths.
  const VarDecl *Root = nullptr;
  llvm::SmallVector<const FieldDecl *, 2> Fields;

  const FieldDecl *leafField() const {
    assert(!Fields.empty() && "leafField() on empty path");
    return Fields.back();
  }
  bool operator==(const MemberAccessPath &O) const {
    return Root == O.Root && Fields == O.Fields;
  }
  // DenseMap::operator== on maps of paths compares values with !=.
  bool operator!=(const MemberAccessPath &O) const { return !(*this == O); }
};

/// Whether Path starts with Prefix: same root, and Path's fields begin with
/// Prefix's.
bool pathHasPrefix(const MemberAccessPath &Path,
                   const MemberAccessPath &Prefix);

/// E without the nodes -fbounds-safety wraps around an access (opaque values,
/// bounds checks, materialized count operands, pointer promotion): none of
/// them changes the pointer value.
const Expr *ignoreBoundsSafetyWrappers(const Expr *E);

/// The path a member access chain names, or nullopt if it has no field or
/// its root is not a variable or `this`. `(*p).f` is the same path as `p->f`.
/// StepThrough lets a caller continue the chain through other nodes (a C++
/// smart pointer's operator->), returning the expression to continue at or
/// null; RootOf maps the root variable (a reference to its referent).
std::optional<MemberAccessPath> decomposeMemberAccess(
    const Expr *E,
    llvm::function_ref<const Expr *(const Expr *)> StepThrough = nullptr,
    llvm::function_ref<const VarDecl *(const VarDecl *)> RootOf = nullptr);

} // namespace clang

// DenseMapInfo needs only a hash and an equality predicate; DenseMap has no
// sentinel keys.
template <> struct llvm::DenseMapInfo<clang::MemberAccessPath> {
  static unsigned getHashValue(const clang::MemberAccessPath &P) {
    unsigned H = DenseMapInfo<const clang::VarDecl *>::getHashValue(P.Root);
    for (const auto *FD : P.Fields)
      H = llvm::hash_combine(
          H, DenseMapInfo<const clang::FieldDecl *>::getHashValue(FD));
    return H;
  }
  static bool isEqual(const clang::MemberAccessPath &L,
                      const clang::MemberAccessPath &R) {
    return L == R;
  }
};

#endif // LLVM_CLANG_ANALYSIS_ANALYSES_MEMBERACCESSPATH_H
