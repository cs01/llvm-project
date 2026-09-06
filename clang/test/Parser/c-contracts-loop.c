// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

int impure(int);
struct Big { int a[4]; };

void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    loop_invariant (i <= len)
    decreases      (len - i)
  {
    buf[i] = 0;
    i++;
  }
}

// The follow-set rule: a clause sequence must be followed by a compound
// statement, so this stays the call statement it has always been rather than
// silently becoming a contract.
int loop_invariant(int);
void still_a_call(int x) {
  while (x) loop_invariant(x);
}

// Predicates are pure here too.
void impure_invariant(int n) {
  while (n)
    loop_invariant (impure(n) > 0) // expected-error {{contract predicate must be free of side effects}}
    // expected-note@3 {{mark 'impure' 'const' or 'pure' to allow calling it from a contract predicate}}
  {
    n--;
  }
}

// A 'decreases' is a measure, not a condition, so it must be scalar.
void bad_decreases(int n, struct Big b) {
  while (n)
    decreases      (b) // expected-error {{loop 'decreases' must be a scalar measure; 'struct Big' is not scalar}}
  {
    n--;
  }
}

// The follow-set rule holds for every loop form.
void calls_in_all_forms(int x) {
  for (;x;) loop_invariant(x);
  do loop_invariant(x); while (x);
}
