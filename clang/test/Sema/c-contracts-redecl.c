// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// The normal shape: contracts on the prototype, definition restates nothing.
int decl_then_def(int *p) pre (p != 0);
int decl_then_def(int *p) { return *p; }

// Contracts on the definition alone are fine too.
int def_only(int n) pre (n > 0) { return n; }

// An equivalent restatement is accepted, including when parameter names differ.
int restated(int *p) pre (p != 0);
int restated(int *p) pre (p != 0) { return *p; }

int renamed(int *p) pre (p != 0);
int renamed(int *q) pre (q != 0) { return *q; }

int post_restated(int n) post (r: r >= old(n));
int post_restated(int value) post (answer: answer >= old(value)) {
  return value;
}

void assigns_restated(int *p, int n) assigns (p[0 : n]);
void assigns_restated(int *q, int count) assigns (q[0 : count]) {}

// A different restatement is rejected.
int two_protos(int n) pre (n > 0); // expected-note {{previous declaration is here}}
int two_protos(int n) pre (n < 9); // expected-error {{contract clauses on this redeclaration do not match the previous declaration}}

// A plain redeclaration with no contracts is untouched.
int decl_then_def(int *p);
