// Function pointers and '_Unchecked' pointers in structs.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN:   -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %s
#define _Owned [[clang::annotate_type("safec_owned")]]
#define _Static [[clang::annotate_type("safec_static")]]
#define _Unchecked [[clang::annotate_type("safec_unchecked")]]
#define _Trusted __attribute__((annotate("safec_trusted")))
#define __single __attribute__((__single__))
#define _Take(p) __builtin_safec_take(p)
void free(void *_Nullable);
struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };

// Accepted.
struct ops { int (*add)(int, int); const char *_Static name; };
static int plus(int a, int b) { return a + b; }
static int minus(int a, int b) { return a - b; }
static const struct ops table[] = {{plus, "plus"}, {minus, "minus"}};
static struct ops current = {plus, "plus"};
int run(int i) {
  struct ops local = {minus, "minus"};
  int (*f)(int, int) = table[i].add;
  current.add = f;
  current.name = local.name;
  return current.add(1, 2) + local.add(3, 4);
}
struct tree {
  struct tree *_Owned _Nullable kid;
  struct tree *_Unchecked _Nullable up;
  int v;
};
_Trusted int up_value(const struct tree *_Unchecked _Nullable u) { return u ? u->v : 0; }
void attach(struct tree *p, struct tree *_Owned k) {
  k->up = p;
  p->kid = k;
}
int parent_is(const struct tree *t, const struct tree *p) {
  struct tree *_Unchecked _Nullable up = t->up;
  return up == p ? up_value(up) : 0;
}

// Out parameters: any pointer may be put there, and only there.
int position(const struct node *n, const struct node *_Unchecked _Nullable *_Nullable end) {
  if (end)
    *end = n;
  return n->v;
}
int handed_on(const struct node *n, const struct node *_Unchecked _Nullable *_Nullable end) {
  const struct node *__single _Unchecked _Nullable mine = 0;
  return position(n, end) + position(n, &mine) + position(n, 0) + (mine == n);
}

// Rejected.
void clear(struct node *_Unchecked _Nullable *o) { *o = 0; }
void owner_slot(struct list *l) {
  clear(&l->head); // expected-error {{this takes the address of an '_Unchecked' variable}}
}
struct lists { void (*f)(struct list *); };
int changes(struct lists *o, struct list *l) {
  struct node *_Nullable n = l->head;
  o->f(l);
  return n ? n->v : 0; // expected-error {{after the call to 'the function pointer' on line}}
}
int followed(struct tree *t) {
  struct tree *_Unchecked _Nullable up = t->up;
  if (!up) return 0;
  return up->v; // expected-error {{an '_Unchecked' pointer is followed only inside a '_Trusted' function}}
}
void laundered(struct tree *t) {
  struct tree *_Nullable up = t->up; // expected-error {{an '_Unchecked' pointer is followed only}}
}
void freed(struct tree *t) {
  free(t->up); // expected-error {{this is a borrow, not an owner}}
}
void owned(struct tree *t) {
  t->kid = t->up; // expected-error {{this is a borrow, not an owner}}
}
void stale(struct tree *t, struct tree *_Owned p) {
  free(p);
  t->up = p; // expected-error {{copied after it was freed on line}}
}
int param(int *_Unchecked p) {
  return *p; // expected-error {{an '_Unchecked' pointer is followed only}}
}
