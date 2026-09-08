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
int result_is_scoped(void) post (q: q > 0) post (q > -1); // expected-error {{use of undeclared identifier 'q'}}

int trailing_tokens(void)
  post (r: r > 0 r); // expected-error {{unexpected tokens at end of 'post' predicate}}

int first_binding(void) post (previous: previous >= 0);
int later_binding(void)
  post (previous >= 0); // expected-error {{use of undeclared identifier 'previous'}}

// 'pre' and 'post' coexist and keep source order.
int both(int *p) pre (p != 0) post (r: r > 0);

// 'assigns' takes locations, not a predicate; see c-contracts-assigns.c.
int has_writes(int *p) assigns (*p);

// The pointer a load goes through is exempt in 'post': `buf[0]` and `*op` read
// memory shared with the caller, and there is nothing for old() to disambiguate.
int deref_ok(int *buf) post (buf[0] == 0);
int diff_ok(int **op, int **ip) post (*op - *ip >= 8);

// An index is not exempt. It is an ordinary value read, and as ambiguous as
// `post (i > 0)` when the body writes to it -- which is the case the rule is
// for, so pruning the whole subtree under a load would have lost it.
int index_bad(int *buf, int i) post (buf[i] == 0);
// expected-error@-1 {{'post' predicate cannot name parameter 'i' directly}}
// expected-note@-2 {{name the value at function entry with 'old(i)'}}
int arith_bad(int *p, int i) post (*(p + i) == 0);
// expected-error@-1 {{'post' predicate cannot name parameter 'p' directly}}
// expected-note@-2 {{name the value at function entry with 'old(p)'}}
