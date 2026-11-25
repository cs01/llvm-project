//===- SafeCTypes.h - what a Safe C type promises ---------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_CLANG_AST_SAFECTYPES_H
#define LLVM_CLANG_AST_SAFECTYPES_H

#include "clang/AST/Type.h"
#include <string>
#include <utility>
#include <vector>

namespace clang {
class Expr;
class ValueDecl;
class VarDecl;
namespace safec {

/// The mark written on a pointer, without its "safec_": "owned", "static",
/// "unchecked"... Empty if there is none.
llvm::StringRef pointerKind(QualType T);

/// What a pointer type promises beyond its C type: its mark and its bounds.
std::string promise(QualType T);

/// Everything two files must agree on about a symbol they share.
std::string signature(const ValueDecl *VD);

/// The structs an interface points to whose contents this file sees, as
/// name and contents.
std::vector<std::pair<std::string, std::string>>
pointedStructs(const ValueDecl *VD);

/// E without parentheses, implicit casts and the opaque values bounds safety
/// wraps call arguments in.
const Expr *peel(const Expr *E);

/// The local a place is reached from ('l' in 'l->head->next'), or null.
const VarDecl *rootVar(const Expr *E);

} // namespace safec
} // namespace clang

#endif
