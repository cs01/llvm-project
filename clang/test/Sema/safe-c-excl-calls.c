// M2 rules 7 and 8: an exclusive parameter is never reassigned or given
// away, and an argument lent exclusively (or moved) reaches nothing any other
// argument of the same call reaches. The control run (no -safe-c-ownership)
// reports none of these.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; int n; };

void clear(struct list *_Excl l) { free(_Take(l->head)); }
void move_head(struct list *_Excl dst, struct list *_Excl src) {
  dst->head = _Take(src->head);
}
int peek(struct list *_Excl l, const struct node *_Nullable n);
void drop(struct list *_Owned _Nullable l);

void calls(struct list *_Excl a, struct list *_Excl b, struct list *other) {
  move_head(a, b);
  move_head(a, a); // expected-error {{'a' is lent exclusively to this call, so no other argument may reach it (the callee could free or replace what that argument points to); pass it once, or finish with the other use before the call}} expected-error {{'a' is lent exclusively}}
  (void)peek(a, a->head); // expected-error {{'a' is lent exclusively to this call}}
  (void)peek(a, b->head);
  (void)other;
}

void reassign(struct list *_Excl l, struct list *_Excl m) {
  l = m; // expected-error {{'l' is an exclusive borrow of the caller's object, which the caller still owns after the call; assigning a new pointer to it would leave the caller with the wrong object; use a local variable instead}}
}
void give_away(struct list *_Excl m) {
  drop(m); // expected-error {{'m' is an exclusive borrow of the caller's object, which the caller still owns after the call; giving it away would leave the caller with the wrong object}}
}
void free_excl(struct list *_Excl l) {
  free(l); // expected-error {{'l' is an exclusive borrow}}
}
