// Ownership, checked on ClangIR inside the compile (SafeCOwnership.cpp).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN: -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %s
// The same at -O2, where CIR would have lifetime markers.
// RUN: %clang_cc1 -emit-llvm -O2 -o /dev/null -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN: -mllvm -safe-c-ownership=true -mllvm -safe-c-redeclared=false -verify %s
#define _Owned [[clang::annotate_type("safec_owned")]]
#define _Trusted __attribute__((annotate("safec_trusted")))
#define _Take(p) __builtin_safec_take(p)
void *_Nullable calloc(unsigned long, unsigned long) __attribute__((alloc_size(1, 2)));
void free(void *_Nullable);
void abort(void) __attribute__((noreturn));

struct node { int v; struct node *_Owned _Nullable next; };
struct list { struct node *_Owned _Nullable head; };
// Frees a node and the rest of its list.
static void node_free(struct node *_Owned _Nullable n) {
  if (!n)
    return;
  node_free(_Take(n->next));
  free(n);
}

// Accepted.
void push(struct list *l, struct node *_Owned n) {
  node_free(_Take(n->next));
  n->next = _Take(l->head);
  l->head = n;
}
int sum(const struct list *l) {
  int s = 0;
  for (const struct node *_Nullable c = l->head; c; c = c->next)
    s += c->v;
  return s;
}
struct node *_Nullable first(const struct list *l) { return l->head; }
void set(struct list *l) {
  struct node *_Nullable n = first(l);
  if (n)
    n->v = 3;
}
struct node *_Owned _Nullable add(struct node *p, struct node *_Owned n) {
  if (p->next)
    return n;
  p->next = n;
  return 0;
}
void use(struct node *p, struct node *_Owned n) {
  struct node *_Owned _Nullable back = add(p, n);
  node_free(back);
}
_Trusted void swap(struct node *a, struct node *b) {
  struct node *_Nullable t = a->next;
  a->next = b->next;
  b->next = t;
}

// Rejected.
static void twice(struct list *l, struct node *_Owned n) { // unused, still checked
  node_free(_Take(l->head));
  l->head = n;
  node_free(n); // expected-error {{passed to 'node_free' after it was stored on line}}
}
int stale(struct list *l) {
  struct node *_Nullable n = l->head;
  node_free(_Take(l->head));
  return n ? n->v : 0; // expected-error {{after '_Take' on line}}
}
void through_const(const struct list *l) {
  struct node *_Nullable n = first(l);
  if (n)
    node_free(_Take(n->next)); // expected-error {{this pointer is 'const'}}
}
int other(struct list *l, const struct list *k) {
  struct node *_Nullable n = first(k);
  node_free(_Take(l->head)); // another root
  return n ? n->v : 0;
}
int aliased(struct list *l) {
  return other(l, l); // expected-error {{reach the same owner}}
}
void touch(char *p);
char peek(char *_Owned p, const char *_Owned q) {
  free(p);
  touch(p); // expected-error {{after it was freed on line}}
  return q[0];
}
void copies(char *_Owned p) {
  free(p);
  __builtin_memset(p, 0, 1); // expected-error {{after it was freed on line}}
}
void believed(int c) {
  if (c)
    abort();
}
// A cast between pointers to numbers keeps the borrow.
unsigned char as_bytes(char *_Owned p) {
  unsigned char *q = (unsigned char *)p;
  free(p);
  return *q; // expected-error {{'q' is read after it was freed on line}}
}
