// The fourth blind attack (2026-11-25): counts, strings, formats and the order
// of evaluation. Each was accepted.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -ferror-limit=0 -fsyntax-only -Xclang -verify %s
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Order of evaluation.
static int gg[4];
struct X { int *_Static __counted_by(4) p; long pad[4]; };
int kill(struct X *_Owned x);
int look(const struct X *x);
int sum_order(struct X *_Owned x) { return *(x->p + kill(x)); } // expected-error {{C does not say which side of this runs first}}
int sum_fine(struct X *x) { return *(x->p + look(x)); }
int step_fine(int *__counted_by(n) p, int n) { int i = 0; return n > 1 ? p[i++] : 0; }
struct W { int *_Static _Nullable __bidi_indexable p; };
int wide(int *_Nullable __bidi_indexable a, int k);
int gone(struct W *_Owned w);
int arg_order(struct W *_Owned w) { return wide(w->p, gone(w)); } // expected-error {{C does not say which side of this runs first}}

// Counts.
struct h { int n; const int *_Static __counted_by(n) f; };
static const int A[1], B[3];
int index_group(void) {
  struct h x[2] = {{1, A}, {1, A}};
  x[0].n = 3; // expected-error {{through a plain variable}}
  x[1].f = B; // expected-error {{through a plain variable}}
  return x[0].f[2];
}
int base_group(void) {
  struct h x = {1, A}, y = {1, A};
  struct h *p = &x;
  p->n = 3;
  p = &y; // expected-error {{set together, with nothing between them}}
  p->f = B;
  return x.f[2];
}
void group_fine(struct h *p) {
  p->n = 3;
  p->f = B;
}
void behind(int *__counted_by(*n) p, int *n, int *m) { // expected-error {{a count behind a pointer}}
  *m = 5;
  p[4] = 1;
}
void before(void) {
  int a[2] = {0};
  int m = 2;
  int *q = &m; // expected-error {{is the count of another field}}
  int *__counted_by(m) p = a;
  *q = 3;
  p[1] = 1;
}
struct two { unsigned n; int *_Owned _Nullable __counted_by(n) f; int *_Owned _Nullable __counted_by(4 - n) g; };
struct same { unsigned n; int *_Owned _Nullable __counted_by(n) f; int *_Owned _Nullable __counted_by(n) g; };
void take(struct two *h, struct same *s) {
  free(_Take(h->f)); // expected-error {{is also the count of another pointer}}
  free(_Take(s->f));
}

// Formats.
void scans(int *__counted_by(4) p, struct h *h) {
  int x, a[4];
  char b[8];
  sscanf("7", "%d", &p[1]); // expected-error {{scanf writes whatever the address is}}
  sscanf("7", "%d", &h->n); // expected-error 1+ {{}}
  sscanf("7 a", "%d %7s", &x, b);
  sscanf("7", "%d", &a[1]); // expected-error {{scanf writes whatever the address is}}
}
void prints(int c) {
  static const char fmt[] = "%n"; // expected-note 0+ {{}}
  printf(fmt, c); // expected-error 1+ {{}} expected-note 0+ {{}}
  printf(c ? "%d" : "%x", c); // expected-error {{the format is a string literal}}
  printf("%d", c);
}

// Strings.
unsigned long reads(const char *s _Noescape);
unsigned long writes(const char *s _Noescape, char *__counted_by(4) b);
unsigned long aliased(void) {
  char b[4] = "abc", c[4] = "abc";
  unsigned long n = writes(b, b); // expected-error {{checked for its terminator before the call}}
  return n + writes(b, c) + reads(b);
}
unsigned long result(void) {
  char b[4] = "abc";
  char *__null_terminated _Nullable s = strchr(b, 'b'); // expected-error {{the result is a string inside a buffer}}
  b[3] = 'x';
  return s ? strlen(s) : 0;
}
int later(void) {
  char b[4] = "abc";
  return strcmp(b, (b[3] = 'x', "abc")); // expected-error {{this argument could change it}}
}
void stepped(char *__null_terminated s) { ++*s; } // expected-error {{may overwrite the terminator}}
struct term { char g[__null_terminated 4]; };
void bytes(struct term *t) { memset(t, 'x', sizeof *t); } // expected-error {{can forge a pointer}}
enum e : _Bool { E0 };
void narrow(enum e *x) { memset(x, 200, 1); } // expected-error {{can forge a pointer}}
void error(void) { char *__null_terminated _Nullable e = strerror(2); } // expected-error {{drops the 'const'}}
