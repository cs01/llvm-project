// RUN: not %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %s 2>&1 \
// RUN:   | FileCheck %s

void drain(int n) {
  int i = 0;
  do
    assigns (i)
    loop_invariant (i <= n)
  {
    if (i++ < n)
      continue;
  } while (i < n);
}

// CHECK: error: cannot emit this contracted 'do' loop for the prover because 'continue' would change meaning during rewriting
// CHECK-NOT: __CPROVER_loop_invariant
