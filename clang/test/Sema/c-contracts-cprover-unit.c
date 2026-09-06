// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts %s | FileCheck -check-prefix=OFF -allow-empty %s
// OFF-NOT: __CPROVER

// -fcontract-emit-cprover-unit rewrites the whole translation unit rather than
// printing clauses, so the result can be handed straight to goto-cc. Everything
// that is not a contract clause is passed through untouched.
unsigned long clamp(unsigned long x, unsigned long hi)
  pre  (hi > 0)
  post (r: r <= old(hi))
{
  return x > hi ? hi : x;
}
// CHECK:      unsigned long clamp(unsigned long x, unsigned long hi)
// CHECK-NEXT:   __CPROVER_requires(hi > 0)
// CHECK-NEXT:   __CPROVER_ensures(__CPROVER_return_value <= __CPROVER_old(hi))
// CHECK-NEXT: {
// CHECK-NEXT:   return x > hi ? hi : x;
// CHECK-NEXT: }

// Loop clauses are rewritten in place too, in source order.
unsigned count(unsigned n)
{
  unsigned i = 0;
  while (i < n)
    assigns        (i)
    loop_invariant (i <= n)
    decreases      (n - i)
  { i++; }
  return i;
}
// CHECK:      while (i < n)
// CHECK-NEXT:     __CPROVER_assigns(i)
// CHECK-NEXT:     __CPROVER_loop_invariant(i <= n)
// CHECK-NEXT:     __CPROVER_decreases(n - i)

// A declaration with no contracts is emitted verbatim.
int untouched(int a, int b);
// CHECK: int untouched(int a, int b);
