// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// 'ensures' binds the return value. The binding needs the return type, which is
// not known while the declarator is still being built, so the predicate is
// replayed once the FunctionDecl exists. A pointer return exercises the case
// that made eager parsing impossible: the pointer chunk is added to the
// declarator after the function chunk.
int *alloc_or_null(int n) ensures (r: r != 0);
int nonneg(int n) ensures (r: r >= 0);

// The result binding is typed as the return type, so this is a pointer
// comparison and that is a type error on an int.
int bad_result_type(int n) ensures (r: r != (void *)0); // expected-warning {{comparison between pointer and integer ('int' and 'void *')}}

// A definition may carry a ensures too.
int def_with_post(void) ensures (r: r > 0) { return 1; }

// Naming a parameter in 'ensures' is ambiguous between its entry and exit value:
// in C every parameter is by value and a body may mutate its own copy.
int names_param(int n) ensures (r: r <= n); // expected-error {{'ensures' predicate cannot name parameter 'n' directly; a by-value parameter may be named in 'ensures' only through 'old()'}}
// expected-note@-1 {{name the value at function entry with 'old(n)'}}

// Without a result name there is simply nothing bound.
int no_result_name(void) ensures (1 == 1);

// The result name is scoped to its own clause.
int r;
int result_is_scoped(void) ensures (q: q > 0) ensures (r > -1);

// 'requires' and 'ensures' coexist and keep source order.
int both(int *p) requires (p != 0) ensures (r: r > 0);

// 'assigns' is still unimplemented.
int has_writes(int *p) assigns (p); // expected-error {{'assigns' contract clauses are not supported yet}}
