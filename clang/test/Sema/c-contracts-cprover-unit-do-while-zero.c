// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %s 2>&1 \
// RUN:   | FileCheck %s

// A 'do ... while (0)' is macro hygiene, not iteration, but goto-cc gives it a
// backedge and CBMC's --enforce-contract then refuses the whole function. The
// emitted unit drops the loop, except where 'break' or 'continue' would notice.

int g;
int arr[8];

void nested(int n)
  pre (n > 0)
  assigns (g)
{
  do { do { g = n; } while (0); do { g = n + 1; } while (0); } while (0);
}

// CHECK-LABEL: void nested(int n)
// CHECK: __CPROVER_requires(n > 0)
// CHECK: __CPROVER_assigns(g)
// CHECK-NEXT: {
// CHECK-NEXT: {  { g = n; }   { g = n + 1; }  }

void keeps_break(int n) {
  do { if (n) break; g = n; } while (0);
}

// CHECK-LABEL: void keeps_break(int n)
// CHECK-NEXT: do { if (n) break; g = n; } while (0);

void keeps_continue(int n) {
  do { if (n) continue; g = n; } while (0);
}

// CHECK-LABEL: void keeps_continue(int n)
// CHECK-NEXT: do { if (n) continue; g = n; } while (0);

// A 'break' bound to a nested switch never reaches the do-loop, so flattening
// is still sound.
void inner_switch_break(int n) {
  do { switch (n) { case 1: break; default: g = n; } } while (0);
}

// CHECK-LABEL: void inner_switch_break(int n)
// CHECK-NEXT: { switch (n) { case 1: break; default: g = n; } }

// The statement's semicolon is part of what goes. Keeping it would leave
// 'if (c) { } ; else', which is not C -- and surviving that position is the
// whole reason a macro body is written as a do-loop in the first place.
void dangling_else(int c, int n) {
  if (c)
    do { g = n; } while (0);
  else
    g = 0;
}

// CHECK-LABEL: void dangling_else(int c, int n)
// CHECK: if (c)
// CHECK-NEXT: { g = n; }
// CHECK-NEXT: else

// A real loop keeps its backedge whatever its condition looks like.
void real_loop(int n) {
  do { g = n; } while (n-- > 0);
}

// CHECK-LABEL: void real_loop(int n)
// CHECK-NEXT: do { g = n; } while (n-- > 0);

// An annotated 'do' is rewritten to 'while (1) { B; if (!C) break; }' instead;
// the vacuous-loop rewrite must not race it.
void annotated(int n) {
  int i = 0;
  do
    assigns (i)
    loop_invariant (i <= 0)
  { g = i; } while (0);
}

// CHECK-LABEL: void annotated(int n)
// CHECK: while (1)
// CHECK: __CPROVER_loop_invariant(i <= 0)
