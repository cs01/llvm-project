// Fuzzing hole 13 (2026-11-23): a _Lifetimebound getter's result borrows its
// argument's root, so taking or replacing an owned field through that root
// ends it, as a direct field read would be. Also hole 08: an _Exclusive local
// (e = &s) borrows from what it points to, so a take through it ends
// borrows of s. The control run pins the ownership rules off.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct S { int *_Owned _Nullable p; };
int *_Nullable getp(struct S *s _Lifetimebound) { return s->p; }

int getter(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = getp(&s); // expected-error {{'getp' returns a borrow of '&s'}}
  free(_Take(s.p));           // expected-note {{the borrow ends here}}
  return q ? *q : 0;          // expected-note {{later used here}}
}

struct node { int v; struct node *_Owned _Nullable next; };
struct node *_Nullable second(struct node *h _Lifetimebound) { return h->next; }
int list(struct node *_Exclusive h) {
  struct node *_Nullable d = second(h); // expected-error {{'second' returns a borrow of 'h'}}
  free(_Take(h->next));                 // expected-note {{the borrow ends here}}
  return d ? d->v : 0;                  // expected-note {{later used here}}
}

int excl_alias(void) {
  struct S s = {0};
  s.p = malloc(4);
  struct S *_Exclusive e = &s;
  int *_Nullable q = s.p; // expected-error {{this pointer is borrowed from 's.p'}}
  free(_Take(e->p));      // expected-note {{the borrow ends here}}
  return q ? *q : 0;      // expected-note {{later used here}}
}

int fine(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = getp(&s);
  int v = q ? *q : 0;
  free(_Take(s.p));
  return v;
}
