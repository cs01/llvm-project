// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts %s | FileCheck -check-prefix=OFF -allow-empty %s
// OFF-NOT: __CPROVER

// 'invariant' is __CPROVER_loop_invariant and 'variant' is __CPROVER_decreases.
// A function's own clauses are emitted at the declarator and its loop clauses
// once the body is parsed, so the two arrive in source order.
int sum(int n) pre (n > 0) {
  int total = 0;
  int i = 0;
  while (i < n)
    invariant (i <= n)
    variant (n - i)
  {
    total += i;
    i++;
  }
  return total;
}
// CHECK:      /* sum */
// CHECK-NEXT: __CPROVER_requires(n > 0)
// CHECK-NEXT: /* sum: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(i <= n)
// CHECK-NEXT: __CPROVER_decreases(n - i)

// A loop contract does not need a function contract to be emitted.
void count_up(int n) {
  for (int i = 0; i < n; i++)
    invariant (i <= n)
  {
  }
}
// CHECK:      /* count_up: for at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(i <= n)

// goto-instrument rejects loop contracts on a do loop, so the clauses are
// emitted with the rewrite that makes them usable called out.
void drain(int n) {
  int i = 0;
  do
    invariant (i <= n)
  {
    i++;
  } while (i < n);
}
// CHECK:      /* drain: do at line {{[0-9]+}}; needs the do => while (1) { B; if (!C) break; } rewrite before goto-instrument accepts these */
// CHECK-NEXT: __CPROVER_loop_invariant(i <= n)

// Nested loops are emitted outermost first.
void nest(int n) {
  int i = 0;
  while (i < n)
    invariant (i <= n)
  {
    int j = 0;
    while (j < i)
      invariant (j <= i)
    {
      j++;
    }
    i++;
  }
}
// CHECK:      /* nest: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(i <= n)
// CHECK-NEXT: /* nest: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(j <= i)

// 'old' is only a contextual keyword before '(', and 'old()' is rejected
// outside 'post', so an 'old' in a loop clause is an ordinary variable and must
// not be rewritten to __CPROVER_old.
void shadow(int n) {
  int old = 0;
  while (old < n)
    invariant (old <= n)
  {
    old++;
  }
}
// CHECK:      /* shadow: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(old <= n)
