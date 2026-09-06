// RUN: not %clang_cc1 -x c++ -std=c++20 -fc-contracts -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s

// The extension is C-only: 'pre' and 'post' are contextual keywords in exactly
// the position a C++ requires-clause occupies, so enabling it for C++ would
// reinterpret valid code rather than extend the language. Reject the flag up
// front instead of silently changing what this file means.

// CHECK: error: invalid argument '-fc-contracts' not allowed with 'C++'

template <typename T>
  requires (sizeof(T) > 0)
T id(T v) { return v; }

int main() { return id(0); }
