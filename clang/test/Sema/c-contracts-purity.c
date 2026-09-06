// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// A predicate is evaluated more than once by design: at callee entry under
// runtime checking, at every call site by the static checker, and again by the
// CBMC export. It must therefore be free of side effects.

int impure(int);
int pure_fn(int) __attribute__((pure));
int const_fn(int) __attribute__((const));

// Pure and const callees are the "usable in specs" marker; no new attribute.
int calls_pure(int n) pre (pure_fn(n) > 0);
int calls_const(int n) pre (const_fn(n) > 0);

int calls_impure(int n) pre (impure(n) > 0); // expected-error {{contract predicate must be free of side effects}}
// expected-note@7 {{mark 'impure' 'const' or 'pure' to allow calling it from a contract predicate}}

int assigns(int n) pre ((n = 1) > 0); // expected-error {{contract predicate must be free of side effects}}
int increments(int n) pre (n++ > 0);  // expected-error {{contract predicate must be free of side effects}}

// Ordinary side-effect-free predicates are unaffected.
int plain(int *p, int n) pre (p != 0) pre (n > 0 && n < 100);
