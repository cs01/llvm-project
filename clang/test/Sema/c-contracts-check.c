// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -Wno-contract-violation -verify=quiet %s
// quiet-no-diagnostics
//
// Checking is a warning in its own group, so it can be turned off without
// giving up the syntax. The syntax being an error without -fc-contracts is
// covered by Parser/c-contracts-disabled.c.

int *allocate(unsigned long n) pre (n > 0);        // expected-note 1+ {{precondition declared here}}
void use(int *p) pre (p != 0);                     // expected-note 1+ {{precondition declared here}}
void put(int *buf, unsigned long len, unsigned long i, int v)
  pre  (buf != 0)                                   // expected-note {{precondition declared here}}
  pre  (i < len);                                   // expected-note {{precondition declared here}}

void literals(void) {
  allocate(0); // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
  use(0);      // expected-warning {{precondition p != 0 of 'use' is violated by this call}}
  // Every violated clause is reported, not just the first.
  put(0, 4, 9, 1); // expected-warning {{precondition buf != 0 of 'put' is violated by this call}}
                   // expected-warning@-1 {{precondition i < len of 'put' is violated by this call}}
}

void through_a_variable(void) {
  int *p = 0;
  use(p); // expected-warning {{precondition p != 0 of 'use' is violated by this call}}
}

void narrowing(int *q) {
  if (q)
    use(q); // no warning: narrowed non-null on this edge
  if (!q)
    use(q); // expected-warning {{precondition p != 0 of 'use' is violated by this call}}
  if (q != 0)
    use(q); // no warning
  if (q == 0)
    use(q); // expected-warning {{precondition p != 0 of 'use' is violated by this call}}
}

// Not knowing must never produce a report: the pass reports only what it can
// show is violated.
void unknown_is_silence(int c, int *q) {
  int *p = q;
  if (c)
    p = 0;
  use(p); // no warning: the two edges disagree
}

void unknown_argument(int *q) {
  use(q); // no warning: nothing is known about q
}

// A caller's own precondition is an assumption inside its body.
void discharged_by_own_pre(int *b) pre (b != 0) {
  use(b); // no warning
}

// A variable whose address is taken can be written through a pointer the pass
// cannot follow, so it is not tracked at all.
void address_taken(void) {
  int *p = 0;
  int **pp = &p;
  *pp = (int *)1;
  use(p); // no warning
}

// Short-circuit: the right operand need not be decidable.
void guard_then_use(int *p) pre (p != 0) pre (*p > 0);
void short_circuit(void) {
  guard_then_use(0); // expected-warning {{precondition p != 0 of 'guard_then_use' is violated by this call}}
                     // expected-note@-3 {{precondition declared here}}
}

// Contracts declared on a prototype are what a call site is checked against,
// even when the definition restates nothing.
int *allocate(unsigned long n) { return 0; }
void calls_definition(void) {
  allocate(0); // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
}

// A callee's postcondition is assumed after the call. Without this the pass
// only ever catches literal arguments.
int *allocating(unsigned long n) pre (n > 0) post (r: r != 0);
int *unpromising(unsigned long n) pre (n > 0);

void discharged_by_post(void) {
  int *p = allocating(4);
  use(p); // no warning: the callee promised a non-null result
}

void through_an_assignment(void) {
  int *p;
  p = allocating(4);
  use(p); // no warning
}

void no_post_is_not_a_promise(void) {
  int *p = unpromising(4);
  use(p); // no warning either: unknown, not disproved
}

void post_then_overwritten(void) {
  int *p = allocating(4);
  p = 0;
  use(p); // expected-warning {{precondition p != 0 of 'use' is violated by this call}}
}

// A loop that establishes the precondition must not be reported. The pass used
// to skip back-edge predecessors, leaving the loop header holding the pre-loop
// state, so a fact the body killed survived to the exit edge and the call was
// reported with n == 10.
void loop_establishes(void) {
  int n = 0;
  for (int i = 0; i < 10; i++)
    n = i + 1;
  allocate(n); // no warning
}

// The same shape through a pointer: the body makes p non-null on every path.
void loop_establishes_pointer(int *q, int fallback_ok) {
  int *p = 0;
  for (int i = 0; i < 4; i++)
    p = fallback_ok ? q : q;
  (void)p;
}

// Constant folding is clang's, not a hand-rolled subset, so every ordinary
// constant expression reaches the predicate.
enum { CAP = 8 };
static const int Zero = 0;
void folds(void) {
  allocate(CAP - 8);   // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
  allocate(Zero);      // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
  allocate(0 ? 1 : 0); // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
  allocate((int)0);    // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
  allocate(sizeof(int) - sizeof(int)); // expected-warning {{precondition n > 0 of 'allocate' is violated by this call}}
}
