// RUN: not %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
// RUN:   -fcontract-emit-harness %s 2>&1 | FileCheck %s

typedef unsigned long size_t;

void conflicting(char *p, size_t n)
  pre (n < 16)
  pre (fresh(p, n))
  pre (fresh(p, n + 1))
  assigns (p[0 : n])
{}

// CHECK: error: cannot generate a proof harness because 'fresh' constraints for p use different sizes
// CHECK-NOT: __contract_harness_conflicting
