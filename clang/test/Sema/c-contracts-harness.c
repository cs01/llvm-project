// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit \
// RUN:   -fcontract-emit-harness %s | FileCheck %s

// The harness is the one artifact an author should never write by hand: a
// hand-written __CPROVER_assume is an assumption nobody reviews. Generating it
// from the contract keeps the reviewable statement in the source, and avoids
// --enforce-contract, which demands a contract on every loop-shaped construct
// in the function -- eleven of them for zlib's inflate_table.
typedef unsigned long size_t;

// A size is usually constrained by another clause, so the assumptions have to
// come before the allocation that uses one -- hence the CHECK-NEXT order below.
void zero(char *p, size_t n)
  pre (fresh(p, n))
  pre (n > 0 && n < 64)
{ for (size_t i = 0; i < n; i++) p[i] = 0; }

// CHECK:      void __contract_harness_zero(void) {
// CHECK-NEXT:   char * p;
// CHECK-NEXT:   size_t n;
// CHECK-NEXT:   __CPROVER_assume(n > 0 && n < 64);
// CHECK-NEXT:   p = __CPROVER_allocate(n, 0);
// CHECK-NEXT:   zero(p, n);
// CHECK-NEXT: }

// A declaration with no body gets no entry point: there is nothing to call.
void declared_only(char *p, size_t n) pre (fresh(p, n));
// The rewriter echoes this file, so any literal spelling of the name matches
// the directive's own text. {{ }} is a regex, which breaks the literal.
// CHECK-NOT: void __contract_harness_dec{{l}}ared_only
