// Bounds holes of the second blind attack (blind2), each run out of bounds before.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct flex { int n; int d[__counted_by(n)]; };
struct cnt { const int *__counted_by(n) p; int n; };
int overflow_out(int c, int *__counted_by(4) p, struct cnt *x) {
  int ok;
  int r = __builtin_mul_overflow(c, 2, &ok);
  r |= __builtin_mul_overflow(c, 1, &p[c]); // expected-error {{writes through this pointer unchecked}}
  r |= __builtin_add_overflow(c, 2, &x->n); // expected-error {{is the count of another field}} expected-error {{writes through this pointer unchecked}}
  return r + ok;
}
double math_out(double d, double *__counted_by(2) p, int *__counted_by(2) e) {
  double whole; int ex;
  double r = modf(d, &whole) + frexp(d, &ex);
  r += modf(d, p + 1);       // expected-error {{writes through this pointer unchecked}}
  return r + frexp(d, &e[1]); // expected-error {{writes through this pointer unchecked}}
}
void dynamic(unsigned n) {
  char *p = __builtin_alloca(n); // expected-error {{'alloca' is checked only with a constant size}}
  p[0] = 1;
}
int vla(int n) {
  int a[4] = {0};
  int (*p)[n] = &a; // expected-error 1+ {{is not checked against what it points to}}
  return (*p)[0];
}
int bytes(struct flex *f, const char *__counted_by(4) b) {
  memcpy(f, b, 4); // expected-error 1+ {{keeps the length of its own array}}
  return f->d[0];
}
void done(struct flex *_Nullable f) { free(f); }
void show(char *p) { printf("%s\n", p); } // expected-error {{nothing says this pointer has one}}
void show_ok(const char *p, char *__null_terminated q) { printf("%s %s\n", p, q); }
__attribute__((format(printf, 1, 2))) int logf_(const char *f, ...) {
  va_list a;
  va_start(a, f);
  f++;
  int r = vprintf(f, a); // expected-error {{nothing checks these arguments against the format}}
  va_end(a);
  return r;
}
void nop(int c) {}
typedef void nr(int) __attribute__((noreturn));
nr *cast(void) { return (nr *)nop; } // expected-error 1+ {{noreturn}} 
