// A struct that holds pointers, kept as a `void *`, hid what it was from the
// byte-write check, so memcpy, memset or read could forge its pointers (hole
// 13 of the 2026-11-23 review). Now it becomes a writable `void *` only in
// the call that needs one, where the check sees its type.
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
void *memcpy(void *__sized_by(n) d, const void *__sized_by(n) s, size_t n);
void *memset(void *__sized_by(n) d, int c, size_t n);
void qsort(void *__sized_by(n * w) base, size_t n, size_t w, int (*cmp)(const void *, const void *));
struct B { int *_Owned _Nullable p; };
struct V { int *_Nullable p; };
struct plain { int a[4]; };
void take(void *p, size_t n);

void *bad(struct B *__bidi_indexable b, struct V *__bidi_indexable v, int c, void *__bidi_indexable other) {
  void *a = b;                // expected-error {{'struct B' holds pointers, and as a 'void *' kept here}}
  void *d = (void *)v;        // expected-error {{'struct V' holds pointers}}
  a = c ? other : (void *)b;  // expected-error {{'struct B' holds pointers}}
  void (*f)(void *, size_t) = take;
  f(b, 1);                    // expected-error {{'struct B' holds pointers}}
  memset((void *)b, 1, sizeof *b); // expected-error {{filling 'struct B'}}
  (void)a; (void)d;
  return b;                   // expected-error {{'struct B' holds pointers}}
}
void sorts(struct B *__counted_by(n) b, size_t n, int (*cmp)(const void *, const void *)) {
  qsort(b, n, sizeof *b, cmp);
  qsort(b, n, 1, cmp); // expected-error {{the element size must be the size of 'struct B'}}
}
void good(struct B *__bidi_indexable b, struct plain *__bidi_indexable p) {
  void *v = p;
  const void *c = b;
  memset(v, 1, sizeof *p);
  memset(b, 0, sizeof *b);
  (void)(c == b);
}
