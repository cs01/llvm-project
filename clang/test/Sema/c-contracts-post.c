// RUN: %clang_cc1 -fsyntax-only -fc-contracts -verify %s

// 'post' binds the return value. The binding needs the return type, which is
// not known while the declarator is still being built, so the predicate is
// replayed once the FunctionDecl exists. A pointer return exercises the case
// that made eager parsing impossible: the pointer chunk is added to the
// declarator after the function chunk.
int *alloc_or_null(int n) post (r: r != 0);
int nonneg(int n) post (r: r >= 0);

// The result binding is typed as the return type, so this is a pointer
// comparison and that is a type error on an int.
int bad_result_type(int n) post (r: r != (void *)0); // expected-warning {{comparison between pointer and integer ('int' and 'void *')}}

// A definition may carry a post too.
int def_with_post(void) post (r: r > 0) { return 1; }

// Naming a parameter in 'post' is ambiguous between its entry and exit value:
// in C every parameter is by value and a body may mutate its own copy.
int names_param(int n) post (r: r <= n); // expected-error {{'post' predicate cannot name parameter 'n' directly; a by-value parameter may be named in 'post' only through 'old()'}}
// expected-note@-1 {{name the value at function entry with 'old(n)'}}

// Without a result name there is simply nothing bound.
int no_result_name(void) post (1 == 1);

// The result name is scoped to its own clause.
int r;
int result_is_scoped(void) post (q: q > 0) post (r > -1);

// 'pre' and 'post' coexist and keep source order.
int both(int *p) pre (p != 0) post (r: r > 0);

// 'assigns' takes locations, not a predicate; see c-contracts-assigns.c.
int has_writes(int *p) assigns (*p);
