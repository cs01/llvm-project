// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-runtime-checks -verify %s

int result(int n)
  pre (n > 0)
  post (r: r > 0) // expected-warning {{'post' contract clauses are not checked at run time}}
  assigns (); // expected-warning {{'assigns' contract clauses are not checked at run time}}

void loop(int n) {
  while (n)
    assigns (n) // expected-warning {{'assigns' contract clauses are not checked at run time}}
    loop_invariant (n >= 0) // expected-warning {{'loop_invariant' contract clauses are not checked at run time}}
    decreases (n) // expected-warning {{'decreases' contract clauses are not checked at run time}}
  {
    --n;
  }
}
