//===- IRCompare.h - Prove a rewrite left the IR unchanged ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The comparison behind cir-modernize-ircmp, for tools that prove their own
// edits. What it undoes before comparing is described in IRCompare.cpp.
//
//===----------------------------------------------------------------------===//

#ifndef CLANG_CIR_MODERNIZE_IRCOMPARE_H
#define CLANG_CIR_MODERNIZE_IRCOMPARE_H

#include "llvm/Support/raw_ostream.h"
#include <string>

namespace llvm {
class Module;
} // namespace llvm

namespace clang::cirmodernize {

enum class IRMatch { Match, Differ, Error };

/// Compares the IR of a translation unit before (Orig) and after (Next) a
/// rewrite; both modules are modified. On Differ, Why holds the first
/// difference; on Error, why normalization failed. With Print, both modules
/// are printed as compared.
IRMatch compareModules(llvm::Module &Orig, llvm::Module &Next, bool Refine,
                       std::string &Why, llvm::raw_ostream *Print = nullptr);

} // namespace clang::cirmodernize

#endif // CLANG_CIR_MODERNIZE_IRCOMPARE_H
