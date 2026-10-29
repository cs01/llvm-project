//===- cir-modernize-ircmp.cpp - Prove a rewrite left the IR unchanged ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Compares the -O0 IR of a translation unit before and after cir-modernize
// rewrote it; see clang/lib/CIR/Modernize/IRCompare/IRCompare.cpp for what is
// undone before the modules must print identically, and what --refine does.
//
// Exits 0 when the modules match, 1 with the first differing line otherwise.
//
//===----------------------------------------------------------------------===//

#include "clang/CIR/Modernize/IRCompare.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/SourceMgr.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;
using clang::cirmodernize::IRMatch;

static cl::opt<std::string> OrigPath(cl::Positional, cl::Required,
                                     cl::desc("<original.ll>"));
static cl::opt<std::string> NextPath(cl::Positional, cl::Required,
                                     cl::desc("<rewritten.ll>"));
static cl::opt<bool> Print("print",
                           cl::desc("print both modules as compared"));
static cl::opt<bool> Refine("refine",
                            cl::desc("Normalize both modules first; see "
                                     "IRCompare.cpp"));

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);
  cl::ParseCommandLineOptions(argc, argv,
                              "prove a cir-modernize rewrite kept the IR\n");
  LLVMContext CA, CB;
  SMDiagnostic Err;
  std::unique_ptr<Module> Orig = parseIRFile(OrigPath, Err, CA);
  if (!Orig) {
    Err.print(argv[0], errs());
    return 2;
  }
  std::unique_ptr<Module> Next = parseIRFile(NextPath, Err, CB);
  if (!Next) {
    Err.print(argv[0], errs());
    return 2;
  }
  std::string Why;
  switch (clang::cirmodernize::compareModules(*Orig, *Next, Refine, Why,
                                              Print ? &outs() : nullptr)) {
  case IRMatch::Match:
    return 0;
  case IRMatch::Differ:
    outs() << Why << "\n";
    return 1;
  case IRMatch::Error:
    errs() << Why << "\n";
    return 2;
  }
  llvm_unreachable("covered switch");
}
