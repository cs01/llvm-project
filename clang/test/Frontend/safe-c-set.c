// '_Set' sets a pointer and its count together.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
struct b { size_t n; int *_Owned _Nullable __counted_by(n) p; };
void ok(struct b *x, int *_Owned _Nullable __counted_by(n) p, size_t n, int c) {
  if (c) {
    free(_Take(x->p));
    _Set(x->p, x->n, p, n);
  } else
    free(p);
}
struct v { size_t n; int *_Static _Nullable __counted_by(n) p; };
void between(struct v *x, int *_Static __counted_by(n) p, size_t n) {
  int k;
  x->p = p;
  k = 1; // expected-error {{with nothing between them; move this before or after them, or use '_Set'}}
  x->n = n;
  if (n)
    x->p = p, k = 2, x->n = n; // expected-error {{with nothing between them}}
}
void two(struct v *x, struct v *y, int *_Static __counted_by(n) p, size_t n) {
  _Set(x->p, y->n, p, n); // expected-error 2 {{requires corresponding assignment}}
}
