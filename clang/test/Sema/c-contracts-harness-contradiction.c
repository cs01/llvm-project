// RUN: not %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
// RUN:   -fcontract-emit-harness %s 2>&1 | FileCheck %s

void impossible(int n)
  pre (n > 10)
  pre (n < 5)
  assigns ()
{}

// CHECK: error: cannot generate a proof harness because this precondition contradicts an earlier precondition
// CHECK-NOT: __contract_harness_impos{{s}}ible
