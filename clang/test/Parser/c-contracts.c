// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

int with_pre(int *p, int n) requires (p != 0) requires (n > 0);

// Clauses conjoin in source order, and a later clause may rely on an earlier
// one, so both are kept and both are checked.
int two_clauses(int *p) requires (p != 0) requires (*p > 0) { return *p; }

// A definition may carry its own clauses.
int on_definition(int n) requires (n >= 0) { return n; }

// The predicate names the parameters, so the prototype scope must still be
// open where the clause is parsed.
int names_params(int a, int b) requires (a < b);

// Parameters of a prototype need not be named; an unnamed one simply cannot be
// referred to.
int unnamed_param(int, int b) requires (b > 0);

int unknown_name(int a) requires (nosuch > 0); // expected-error {{use of undeclared identifier 'nosuch'}}

struct T { int x; };
int struct_predicate(struct T t) requires (t); // expected-error {{statement requires expression of scalar type ('struct T' invalid)}}

// 'requires' is contextual: it is still an ordinary identifier everywhere else.
int requires = 3;
int uses_pre_as_name(void) { return requires; }
int pre_typedef_ok(int requires) requires (requires > 0);

// 'ensures' is implemented; see c-contracts-ensures.c. 'assigns' takes a list
// of locations rather than a predicate; see c-contracts-assigns.c.
int has_post(int n) ensures (r: r > 0);
int has_writes(int *p) assigns (*p);

// A clause is only allowed on a declarator that declares a function. Here the
// declarator parses a function type but declares a pointer variable.
int (*fnptr)(int a) requires (a > 0); // expected-error {{contract clause is only allowed on a function declaration}}
