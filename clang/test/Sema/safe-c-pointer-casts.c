// Under -fsafe-c, pointer casts follow the rule the Lean core model needs for
// soundness: allowed to void, between identical types (nullability counted at
// every level), or between two pointer-free types; a __single pointer cannot be
// cast to a larger pointee; void * only from null or a fresh allocation.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify -verify-ignore-unexpected=note %s
// RUN: %clang_cc1 -fsyntax-only -verify=plain -Wno-incompatible-pointer-types \
// RUN:   -Wno-incompatible-function-pointer-types %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void *_Nullable realloc(void *_Nullable, size_t);

struct A { int x; int *p; };
struct B { long y; int *q; };
struct P1 { int a; short b; };
struct P2 { short c; int d; };

void launder(int **pp) {
  char *c = (char *)pp; // expected-error {{can treat memory as the wrong type}}
  void *v = pp; // expected-warning {{with zero length}}
  (void)c; (void)v;
}

void structs(struct A *a, struct P1 *p1) {
  struct B *b = (struct B *)a;    // expected-error {{can treat memory as the wrong type}}
  struct P2 *p2 = (struct P2 *)p1; // ok: both pointer-free
  (void)b; (void)p2;
}

void nullability(int *_Nonnull *pp) {
  int *_Nullable *q = (int *_Nullable *)pp; // expected-error {{can treat memory as the wrong type}}
  (void)q;
}

int f(int *p) { return *p; }
void fnptr(void) {
  int (*g)(long) = (int (*)(long))f; // expected-error {{can treat memory as the wrong type}}
  (void)g;
}

// Separate functions: realloc frees its argument, so a second realloc of the
// same pointer would be a use after free.
void grow_other(long *_Nullable l) {
  int *_Nullable *_Nullable pp = realloc(l, 2 * sizeof(int *)); // expected-error {{can treat memory as the wrong type}}
  (void)pp;
}
void grow_same(long *_Nullable l) {
  long *_Nullable l2 = realloc(l, 2 * sizeof(long)); // ok: same type
  (void)l2;
}

struct S { char c; long secret; };
void widen(char *p) {
  long *l = (long *)p; // expected-error {{can treat memory as the wrong type}}
  (void)l;
}
