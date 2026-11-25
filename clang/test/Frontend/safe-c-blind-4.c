// Blind attack, the last groups and four compiler crashes (2026-11-24).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -ferror-limit=0 -Xclang -verify %s
#include <stdatomic.h>
#include <stdlib.h>
#include <time.h>
struct s { int a, b; };
static _Atomic struct s x;
struct h { int n; };
struct fam { struct h h; int a[__counted_by(h.n)]; }; // expected-note 0+ {{}}
void count(struct fam *f, struct h z) { f->h = z; } // expected-error {{holds the count of another field}} expected-error 0+ {{}} expected-note 0+ {{}}
typedef int v4 __attribute__((vector_size(16)));
int vla(int n, int m, int a[n][m]) { return a[n - 1][m - 1]; } // expected-error {{whose row length is another parameter}}
void alias(int *restrict a, int *b) { *a = *b; } // expected-error {{nothing checks it; leave it out}}
void library(void) { timezone = 3; } // expected-error {{belongs to the C library}}
void member(void) { x.a = 1; } // expected-error {{accessed without synchronization}} expected-error 0+ {{}} expected-warning 0+ {{}}
int lanes(v4 v, int i) { return v[i] + v[2]; } // expected-error {{the lane index of a vector}}
int align(char *__counted_by(n) b, int n) {
  int *p = (int *)(b + 1);                 // expected-error {{needs stricter alignment}}
  _Atomic long *q = (_Atomic long *)b;     // expected-error {{needs stricter alignment}} expected-error 0+ {{}}
  unsigned char *u = (unsigned char *)b;
  return n ? *p + u[0] + (q != 0) : 0;
}
