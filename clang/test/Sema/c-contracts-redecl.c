// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// The normal shape: contracts on the prototype, definition restates nothing.
int decl_then_def(int *p) requires (p != 0);
int decl_then_def(int *p) { return *p; }

// Contracts on the definition alone are fine too.
int def_only(int n) requires (n > 0) { return n; }

// Restating is rejected rather than silently resolved, so a caller cannot get
// a different contract depending on which declaration it saw.
int restated(int *p) requires (p != 0); // expected-note {{previous declaration is here}}
int restated(int *p) requires (p != 0) { return *p; } // expected-error {{contract clauses cannot be restated on a redeclaration}}

// Same rule between two prototypes.
int two_protos(int n) requires (n > 0); // expected-note {{previous declaration is here}}
int two_protos(int n) requires (n < 9); // expected-error {{contract clauses cannot be restated on a redeclaration}}

// A plain redeclaration with no contracts is untouched.
int decl_then_def(int *p);
