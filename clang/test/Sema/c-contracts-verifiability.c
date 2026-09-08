// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s
//
// Contracts the compiler accepts but the prover cannot use. Each case is one
// entry from proofs/contracts-eval/ISSUES.md, hit annotating real code.

#define __always_inline__ __attribute__((always_inline))

// ISSUE 2: an always_inline carrier is inlined before contracts are applied, so
// the clause is accepted and then silently does nothing. zstd's ZSTD_wildcopy.
__attribute__((always_inline)) inline // expected-note {{'always_inline' declared here}}
void wildcopy(char *dst, unsigned long n)
    assigns(dst[0 : n])                 // expected-warning {{contract on 'wildcopy' has no effect on callers}}
{
  for (unsigned long i = 0; i < n; i++)
    assigns        (i, dst[0 : n])
    loop_invariant (i <= n)
    decreases      (n - i)
  { dst[i] = 0; }
}

// ISSUE 7: a contract with no 'assigns' is checked against an empty frame, so
// every write is a violation. nghttp2_buf_reserve's first annotation.
void zero_one(int *p)
    pre(p != 0)     // expected-warning {{'zero_one' has a contract but no 'assigns' clause}}
{
  *p = 0;           // expected-note {{this write is outside the empty frame}}
}

// ISSUE 1: a function contract cannot be checked while a loop in the body has
// none. ZSTD_execSequence, via the loops inlined into it.
void clear(char *p, unsigned long n)
    assigns(p[0 : n])  // expected-warning {{'clear' has a contract but its body contains a loop with no loop contract}}
{
  while (n) {          // expected-note {{this loop needs 'loop_invariant' and 'decreases'}}
    p[--n] = 0;
  }
}

// A fully annotated function is quiet: no frame warning, no loop warning.
void clear_ok(char *p, unsigned long n)
    assigns(p[0 : n])
{
  while (n)
    assigns        (n, p[0 : n])
    decreases      (n)
  { p[--n] = 0; }
}

void declared_zero(int *p) pre(p != 0);
void declared_zero(int *p) // expected-warning@-1 {{'declared_zero' has a contract but no 'assigns' clause}}
{
  *p = 0; // expected-note {{this write is outside the empty frame}}
}

int global;
void writes_global(void) pre(global >= 0) // expected-warning {{'writes_global' has a contract but no 'assigns' clause}}
{
  global = 1; // expected-note {{this write is outside the empty frame}}
}

void writes_through_alias(int *p) pre(p != 0) // expected-warning {{'writes_through_alias' has a contract but no 'assigns' clause}}
{
  int *alias = p;
  *alias = 1; // expected-note {{this write is outside the empty frame}}
}

void writes_local_only(int *p) pre(p != 0)
{
  int local;
  int *alias = &local;
  *alias = 1;
  p = &local;
}

// A contract on a function that writes nothing through a parameter needs no
// frame, so the missing-assigns warning must not fire.
int is_positive(int v)
    pre(v > -100)
{
  return v > 0;
}
