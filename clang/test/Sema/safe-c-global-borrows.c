// M2 rule 14: globals act as one struct lent to every call that may touch
// them (any function not marked pure or const). A pointer read out of an
// owned global is a borrow that such a call ends, even when the borrow is
// passed to that call. The control run (no -safe-c-ownership) reports none.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);
struct node { int v; };
struct node *_Owned _Nullable cur;

void reset(void);
__attribute__((pure)) int score(const struct node *n);
int show(const struct node *n);

int across_call(void) {
  struct node *_Nullable r = cur; // expected-error {{this pointer is borrowed from 'cur', and the borrow ends (the field is taken, replaced, or lent to an exclusive parameter) before its last use; read 'cur' again after that point, or finish using the pointer first}}
  reset(); // expected-note {{the borrow ends here}}
  return r ? r->v : 0; // expected-note {{later used here}}
}

int pure_call_keeps_it(void) {
  struct node *_Nullable r = cur;
  if (!r)
    return 0;
  return score(r) + r->v;
}

int passed_to_impure(void) {
  struct node *_Nullable r = cur; // expected-error {{this pointer is borrowed from 'cur'}}
  if (!r)
    return 0;
  return show(r); // expected-note {{the borrow ends here}} expected-note {{later used here}}
}

void take_global(void) { free(_Take(cur)); }

// Purity is inferred (rule 14.4): a function whose body names no global and
// calls only clean functions does not end borrows of globals.
int counter;
static int clean(const struct node *n) { return n->v + 1; }
static int dirty(const struct node *n) { counter++; return n->v; }
int inferred(void) {
  struct node *_Nullable r = cur;
  if (!r)
    return 0;
  return clean(r) + r->v;
}
int inferred_dirty(void) {
  struct node *_Nullable r = cur; // expected-error {{this pointer is borrowed from 'cur'}}
  if (!r)
    return 0;
  return dirty(r); // expected-note {{the borrow ends here}} expected-note {{later used here}}
}
