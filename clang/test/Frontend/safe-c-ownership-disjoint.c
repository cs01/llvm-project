// Two fields of one struct own separate memory: changing one does not end what
// is borrowed from the other. A pointer that is reassigned is another struct.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
struct box { int *_Owned _Nullable p; };
struct two { struct box a, b; };
struct nd { struct box a; struct nd *_Owned _Nullable next; };
void clr(struct box *b) { free(_Take(b->p)); }
int *_Nullable get(struct box *b) { return b->p; }
void both(struct two *t) { clr(&t->a); clr(&t->b); }
int mix(struct box *x, struct box *y) { clr(y); return get(x) != NULL; }
void drop(struct nd *_Owned _Nullable n) {
  if (!n) return;
  clr(&n->a);
  drop(_Take(n->next));
  free(n);
}

int other(struct two *t) {
  int *_Nullable p = get(&t->a);
  clr(&t->b);
  return p ? *p + mix(&t->a, &t->b) : 0;
}
int local(void) {
  struct two t = {0};
  int *_Nullable p = get(&t.a);
  clr(&t.b);
  int r = p ? *p : 0;
  both(&t);
  return r;
}
int same(struct two *t) {
  int *_Nullable p = get(&t->a);
  clr(&t->b);
  clr(&t->a);
  return p ? *p : 0; // expected-error {{'p' is tested after the call to 'clr'}}
}
int whole(struct two *t) {
  int *_Nullable p = get(&t->a);
  clr(&t->b);
  both(t);
  return p ? *p : 0; // expected-error {{'p' is tested after the call to 'both'}}
}
int alias(struct two *t) {
  struct two *u = t;
  int *_Nullable p = get(&t->a);
  clr(&u->a); // expected-error {{'u' is used after the call to 'get'}}
  return p ? *p : 0;
}
int args(struct two *t) {
  return mix(&t->a, &t->a); // expected-error {{two arguments of 'mix' reach the same owner}}
}
int moved(struct nd *n) {
  struct nd *c = n;
  struct nd *_Nullable d = c->next;
  if (!d) return 0;
  c = d;
  struct box *a = &c->a;
  c = n;
  drop(_Take(c->next));
  return get(a) != NULL; // expected-error {{'a' is passed to 'get' after '_Take'}}
}
int loop(struct nd *n) {
  struct nd *_Nullable c = n;
  struct box *_Nullable a = NULL;
  for (int i = 0; i < 2 && c; i++) {
    a = &c->a;
    c = c->next;
  }
  drop(_Take(n->next));
  return a ? get(a) != NULL : 0; // expected-error {{'a' is tested after '_Take'}}
}
int branch(struct two *t, int k) {
  struct box *x = &t->a;
  if (k) x = &t->b;
  int *_Nullable p = get(x);
  clr(&t->b);
  return p ? *p : 0; // expected-error {{'p' is tested after the call to 'clr'}}
}
int twice(struct two *t) {
  struct box *x = &t->a;
  clr(&t->b);
  int *_Nullable p = get(x);
  clr(&t->b);
  clr(x);
  return p ? *p : 0; // expected-error {{'p' is tested after the call to 'clr'}}
}
