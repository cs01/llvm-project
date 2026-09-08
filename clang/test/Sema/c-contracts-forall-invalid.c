// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

typedef unsigned long size_t;
struct S { int a; };

int bad_bound(const char *p, struct S s)
  pre (forall (i : 0, s) p[i] == 0); // expected-error {{'forall' bound must have integer type, not 'struct S'}}

int bad_name(const char *p, size_t n)
  pre (forall (0 : 0, n) p[0] == 0); // expected-error {{expected the name of the bound variable in a 'forall' clause}}

// The bound variable is not in scope in its own bounds.
int bad_scope(const char *p, size_t n)
  pre (forall (i : 0, i) p[0] == 0); // expected-error {{use of undeclared identifier 'i'}}

// Nor after the clause.
int leaked(const char *p, size_t n)
  pre (forall (i : 0, n) p[i] == 0)
  pre (i == 0); // expected-error {{use of undeclared identifier 'i'}}
