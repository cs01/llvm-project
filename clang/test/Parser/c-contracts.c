// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

int with_pre(int *p, int n) pre (p != 0) pre (n > 0);

// Clauses conjoin in source order, and a later clause may rely on an earlier
// one, so both are kept and both are checked.
int two_clauses(int *p) pre (p != 0) pre (*p > 0) { return *p; }

// A definition may carry its own clauses.
int on_definition(int n) pre (n >= 0) { return n; }

// The predicate names the parameters, so the prototype scope must still be
// open where the clause is parsed.
int names_params(int a, int b) pre (a < b);

// Parameters of a prototype need not be named; an unnamed one simply cannot be
// referred to.
int unnamed_param(int, int b) pre (b > 0);

int unknown_name(int a) pre (nosuch > 0); // expected-error {{use of undeclared identifier 'nosuch'}}

struct T { int x; };
int struct_predicate(struct T t) pre (t); // expected-error {{statement requires expression of scalar type ('struct T' invalid)}}

// 'pre' is contextual: it is still an ordinary identifier everywhere else.
int pre = 3;
int uses_pre_as_name(void) { return pre; }
int pre_typedef_ok(int pre) pre (pre > 0);

// Only 'pre' is implemented so far. The other keywords are recognized so that
// they are diagnosed rather than silently reinterpreted.
int has_post(int n) post (n > 0);   // expected-error {{'post' contract clauses are not supported yet}}
int has_writes(int *p) writes (p);  // expected-error {{'writes' contract clauses are not supported yet}}

// A clause is only allowed on a declarator that declares a function. Here the
// declarator parses a function type but declares a pointer variable.
int (*fnptr)(int a) pre (a > 0); // expected-error {{contract clause is only allowed on a function declaration}}
