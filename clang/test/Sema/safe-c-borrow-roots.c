// A borrow kept in a plain local pointer is rooted where its values come
// from: in `c = l; c = c->next;` that is `l`. So a cursor walking a list may
// be returned when `l` is lifetimebound, and a borrow read through the
// cursor ends when `l`'s owned fields change. realloc of an owned local or
// parameter is a move in and an owned result out. The control run (no
// -safe-c-ownership) reports none of these.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);
void *_Nullable malloc(size_t);
void *_Nullable realloc(void *_Nullable, size_t);
struct L { int v; struct L *_Owned _Nullable next; };

const struct L *_Nullable last(const struct L *l _Lifetimebound) {
  const struct L *_Nullable c = l;
  while (c && c->next)
    c = c->next;
  return c;
}

int through_cursor(struct L *_Exclusive l) {
  struct L *c = l;
  struct L *_Nullable r = c->next; // expected-error {{this pointer is borrowed from 'c->next'}}
  free(_Take(l->next)); // expected-note {{the borrow ends here}}
  return r ? r->v : 0; // expected-note {{later used here}}
}

_Returns_owned int *_Nullable grow(int *_Nullable _Owned p, size_t n) {
  return realloc(p, n);
}
void grow_local(void) {
  int *_Nullable p = malloc(4);
  p = realloc(p, 8);
  free(p);
}

// Lending a reborrow exclusively reborrows it again: it stays usable.
void clear(struct L *_Exclusive l);
struct L *_Nullable add(struct L *_Exclusive parent _Lifetimebound);
int lend_reborrow(struct L *_Exclusive root) {
  struct L *_Nullable kid = add(root);
  if (!kid)
    return 0;
  clear(kid);
  clear(kid);
  return kid->v;
}
