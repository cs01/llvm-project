// RUN: not %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
// RUN:   -fcontract-emit-harness %s 2>&1 | FileCheck %s

typedef unsigned long size_t;

void ambiguous(char *p, size_t n)
  pre (n < 8)
  pre (!fresh(p, n))
  assigns ()
{}

// CHECK: error: cannot generate a proof harness for 'fresh' in this expression
// CHECK-NOT: __contract_harness_amb{{i}}guous
