// M2 flow layer: a pointer read out of an owned field is a borrow rooted at
// the variable the field is reached from. Taking the field, replacing it, or
// lending the root to an __safec_excl parameter ends the borrow; a later use
// is an error. Ownership operations need an owner or an exclusive borrow.
// The control run (no -safe-c-ownership) reports none of these.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);

struct node { int v; struct node *__safec_owned _Nullable next; };
struct list { struct node *__safec_owned _Nullable head; };

int after_take(struct list *__safec_excl l) {
  struct node *_Nullable r = l->head; // expected-error {{this pointer is borrowed from 'l->head', and the borrow ends (the field is taken, replaced, or lent to an exclusive parameter) before its last use; read 'l->head' again after that point, or finish using the pointer first}}
  free(__safec_take(l->head)); // expected-note {{the borrow ends here}}
  return r ? r->v : 0; // expected-note {{later used here}}
}

int after_replace(struct list *__safec_excl l, struct node *__safec_owned _Nullable n) {
  struct node *_Nullable r = l->head; // expected-error {{this pointer is borrowed from 'l->head'}}
  struct node *_Nullable old = __safec_take(l->head);
  l->head = n; // expected-note {{the borrow ends here}}
  free(old);
  return r ? r->v : 0; // expected-note {{later used here}}
}

void clear(struct list *__safec_excl l) { free(__safec_take(l->head)); }
int after_lend(struct list *__safec_excl l) {
  struct node *_Nullable r = l->head; // expected-error {{this pointer is borrowed from 'l->head'}}
  clear(l); // expected-note {{the borrow ends here}}
  return r ? r->v : 0; // expected-note {{later used here}}
}

int fine(struct list *__safec_excl l) {
  struct node *_Nullable r = l->head;
  int v = r ? r->v : 0; // used before the borrow ends
  clear(l);
  r = l->head; // read again afterwards
  return v + (r ? r->v : 0);
}

int reader(const struct list *l) {
  const struct node *_Nullable r = l->head; // plain borrows may read
  return r ? r->v : 0;
}

void not_exclusive(struct list *l) {
  free(__safec_take(l->head)); // expected-error {{taking from an owned field through 'l' changes what its owner holds while callers may still be using it; declare 'l' '__safec_excl' (an exclusive borrow, like Rust's '&mut')}}
}
