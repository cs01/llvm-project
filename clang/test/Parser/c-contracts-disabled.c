// RUN: %clang_cc1 -fsyntax-only -verify %s

// Without -fc-contracts the clause keywords are ordinary identifiers, so this
// is the same syntax error it has always been.
int f(int *p) requires (p != 0); // expected-error {{expected function body after function declarator}}

int requires = 0;
int g(void) { return requires; }
