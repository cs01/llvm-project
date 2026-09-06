// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover %s | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts %s | FileCheck -check-prefix=OFF -allow-empty %s
// OFF-NOT: __CPROVER

// 'loop_invariant' is __CPROVER_loop_invariant and 'decreases' is __CPROVER_decreases.
// A function's own clauses are emitted at the declarator and its loop clauses
// once the body is parsed, so the two arrive in source order.
int sum(int n) pre (n > 0) {
  int total = 0;
  int i = 0;
  while (i < n)
    loop_invariant (i <= n)
    decreases      (n - i)
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
    loop_invariant (i <= n)
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
    loop_invariant (i <= n)
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
    loop_invariant (i <= n)
  {
    int j = 0;
    while (j < i)
      loop_invariant (j <= i)
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
    loop_invariant (old <= n)
  {
    old++;
  }
}
// CHECK:      /* shadow: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_loop_invariant(old <= n)

// A loop frame prints like a function's, in source order with the other loop
// clauses, which is the order goto-instrument wants them in.
void zero(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns        (i, buf[i])
    loop_invariant (i <= len)
    decreases      (len - i)
  { buf[i] = 0; i++; }
}
// CHECK:      /* zero: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_assigns(i, buf[i])
// CHECK-NEXT: __CPROVER_loop_invariant(i <= len)
// CHECK-NEXT: __CPROVER_decreases(len - i)

// A range lowers to CBMC's byte-counted primitive: the compiler does the
// sizeof multiply, because a frame written one element too small does not fail,
// it silently proves less.
void zero_range(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    assigns        (i, buf[0 : len])
    loop_invariant (i <= len)
  { buf[i] = 0; i++; }
}
// CHECK:      /* zero_range: while at line {{[0-9]+}} */
// CHECK-NEXT: __CPROVER_assigns(i, __CPROVER_object_upto((buf + 0), ((len) - (0)) * sizeof(*buf)))
// CHECK-NEXT: __CPROVER_loop_invariant(i <= len)
