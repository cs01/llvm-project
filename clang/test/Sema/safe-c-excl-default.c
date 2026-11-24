// -mllvm -safe-c-excl-default (off by default, being measured): a plain
// pointer-to-non-const parameter is _Excl, as `&mut T` is in Rust, and a
// `const T *` one is a shared borrow. Lending to it ends the caller's
// borrows of the argument; two arguments may not reach the same data. The
// control run leaves the flag off: there `reset` may not free through a plain
// pointer.
// RUN: %clang_cc1 -mllvm -safe-c-excl-default -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=off %s

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct S { int *_Owned _Nullable p; };
void reset(struct S *s) { free(_Take(s->p)); } // allowed: s is exclusive; off-error {{taking from an owned field through 's'}}
int peek(const struct S *s) { return s->p ? *s->p : 0; }
void pair(struct S *a, struct S *b) { (void)a; (void)b; }

int borrow_ends(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = s.p; // expected-error {{this pointer is borrowed from 's.p'}}
  reset(&s);              // expected-note {{the borrow ends here}}
  return q ? *q : 0;      // expected-note {{later used here}}
}

void aliased(void) {
  struct S s = {0};
  pair(&s, &s); // expected-error 2 {{is lent exclusively to this call}}
}

int shared_ok(void) {
  struct S s = {0};
  s.p = malloc(4);
  int *_Nullable q = s.p;
  int v = peek(&s) + (q ? *q : 0);
  free(_Take(s.p));
  return v;
}

// A plain local whose values all come from an exclusive source is itself
// exclusive (Rust's reborrow): it may be lent on, also after `p += n`.
struct Node { int v; struct Node *_Owned _Nullable next; };
void bump(struct Node *n) { n->v++; }
void out(int **slot);
void reborrow(struct Node *head) {
  struct Node *cur = head;
  bump(cur);
  int x = 0;
  int *__single px = &x;
  out(&px);
}
void walk(char *__counted_by(n) buf, int n) {
  char *p = buf;
  p += n;
  void fill(char *__counted_by(k) d, int k);
  fill(p, 0);
}

// `p = f(p)` with a lifetimebound exclusive parameter: the result replaces p
// (Rust moves the reborrow in and out), so lending p again is fine.
struct Node *next_of(struct Node *n _Lifetimebound);
void chain(struct Node *head) {
  struct Node *cur = head;
  cur = next_of(cur);
  bump(cur);
  cur = next_of(cur);
  bump(cur);
}
