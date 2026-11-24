// M2 rule 8.2: an argument for an _Excl parameter must reach its object only
// through owners and exclusive borrows. A plain local counts as an owner when
// it only ever holds owned values (like Rust's `let x = Box::new(..)`), and as
// an exclusive reborrow when it only holds results of `_Excl _Lifetimebound`
// calls; a reborrow ends when its root is lent again. The control run (no
// -safe-c-ownership) reports none of these.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);
typedef struct N { int v; struct N *_Owned _Nullable child; } N;
_Returns_owned N *_Nullable make(void);
void clear(N *_Excl n);
void drop(N *_Nullable _Owned n);
N *_Nullable add_child(N *_Excl parent _Lifetimebound);

void shared_borrow(N *a) {
  N *_Nullable c = a->child;
  if (c)
    clear(c); // expected-error {{'c' is a shared borrow, but this parameter is '_Excl': the callee may free or replace what it owns while the owner still uses it; pass the owner, or declare 'c' '_Owned' (if it owns the object) or '_Excl'}}
}

void owner_through_excl(N *_Excl a) {
  if (a->child)
    clear(a->child); // an owned field of an exclusive borrow
  clear(a);
}

void inferred_owner(void) {
  N *_Nullable n = make(); // holds only owned values: an owner
  if (!n)
    return;
  clear(n);
  drop(n);
}

void reborrow(N *_Excl root) {
  N *_Nullable kid = add_child(root); // an exclusive reborrow of root
  if (!kid)
    return;
  clear(kid);
}

int reborrow_ended(N *_Excl root) {
  N *_Nullable kid = add_child(root); // expected-error {{'add_child' returns a borrow of 'root', which ends (an owned field of it is taken or replaced, or it is lent exclusively or given away) before this pointer's last use; call 'add_child' again after that point, or finish using the pointer first}}
  clear(root); // expected-note {{the borrow ends here}}
  return kid ? kid->v : 0; // expected-note {{later used here}}
}
