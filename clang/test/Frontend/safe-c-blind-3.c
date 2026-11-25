// Blind attack, owners, tags, builtins and races (2026-11-24).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -mllvm -safe-c-strict-ownership=true -ferror-limit=0 -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -mllvm -safe-c-strict-ownership=true -DRUN -O1 %s -o %t && not --crash %t
#include <safec.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum k { A, B };
static const char *name(enum k k) { switch (k) { case A: return "a"; case B: return "b"; } }
#ifdef RUN
int main(int c, char **v) { (void)v; puts(name((enum k)(c + 6))); return 0; }
#else
struct s { int *_Owned _Nullable p; };
void del(struct s *_Exclusive s);
void eat(struct s s);
struct s *id(struct s *s);
static _Thread_local struct s g;
void cl(int *x) { (void)x; free(_Take(g.p)); }
void h(int s) { (void)s; free(_Take(g.p)); } // expected-note 0+ {{}} expected-error 0+ {{}}
void install(void) { signal(SIGUSR1, h); }   // expected-error 0+ {{}} expected-note 0+ {{}}
int literal(void) {
  int *_Nullable q = malloc(4);
  if (!q) return 1;
  eat((struct s){q}); // expected-error {{moving q into a literal}} expected-error 0+ {{}}
  return 0;
}
void helper(struct s *_Exclusive a) {
  free(_Take(id(a)->p)); // expected-error {{which object this call's result reaches}}
  del(id(a));            // expected-error {{which object this call's result reaches}}
}
int cleanup(void) {
  g.p = malloc(4);
  int *_Nullable q = g.p; // expected-error {{borrow}} expected-note 0+ {{}}
  { int z _Drop(cl) = 1; (void)z; } // expected-note 0+ {{}}
  return q ? *q : 0; // expected-note {{later used here}}
}
struct s ret(void) {
  struct s s _Drop(del) = {0};
  return s; // expected-error {{is cleaned up as this function returns}}
}
void dropfree(void) {
  long x _Drop_free = 3; // expected-error {{must be a pointer to memory from the allocator}} expected-error 0+ {{}}
}
struct t { int k; union _Tagged_by(k) { double p _When(1); long l _When(0); }; };
void tags(int c) {
  struct t t = {.k = c, .l = 77}; // expected-error {{known only at run time}}
  t.k = 1;
  (void)t;
}
struct t paired(void) {
  struct t t = {.k = 0, .l = 77};
  t.k = 1;
  t.p = 1;
  return t;
}
void bytes(struct s *x, const char *m) {
  memset(x, 0, 3); // expected-error {{copied or cleared only whole}}
  char a[4];
  __builtin___snprintf_chk(a, 40, 0, 40, "abcdefgh"); // expected-error {{is a compiler builtin}} expected-warning 0+ {{}} expected-error 0+ {{}}
  int i = 0;
  __atomic_store_n(&i, 1, 0); // expected-error {{is a compiler builtin}}
  char *t = strrchr("abc", 'b'); // expected-error {{drops the 'const'}}
  const char *__null_terminated _Nullable ok = strchr(m, 'b');
  (void)t; (void)ok;
}
static int *_Static const shared = &(int){3}; // expected-error 0+ {{}}
static const struct { int *_Static p; } holder = {&(int){3}}; // expected-error 0+ {{}}
void n_conv(void) {
  char c;
  printf("%n", (int *)&c); // expected-error {{stores through a pointer nothing checks}} expected-error 0+ {{}} expected-note 0+ {{}} expected-warning 0+ {{}}
}
int forged(int n, ...) {
  va_list a;
  va_start(a, n);
  char b[20];
  vsnprintf(b, 20, "%s", a); // expected-error {{nothing checks these arguments against the format}}
  va_end(a);
  return 0;
}
__attribute__((format(printf, 1, 2))) int after_end(const char *m, ...) {
  va_list a;
  va_start(a, m);
  va_end(a);
  return vprintf(m, a); // expected-error {{nothing checks these arguments against the format}}
}
__attribute__((format(printf, 1, 2))) int forward(const char *m, ...) {
  va_list a;
  va_start(a, m);
  int r = vprintf(m, a);
  va_end(a);
  return r;
}
void look(struct t *t);
long peek(struct t *t);
void between(void) {
  struct t t = {.k = 1, .p = 1};
  t.k = 0;
  t.l = peek(&t); // expected-error {{needs the tag}}
}
__attribute__((malloc, alloc_size(1))) char *liar(size_t n) {
  (void)n;
  char *_Nullable p = malloc(1);
  if (!p) abort();
  return p; // expected-error {{is marked 'alloc_size'}}
}
__attribute__((malloc, alloc_size(1))) char *honest(size_t n) {
  char *_Nullable p = malloc(n + 1);
  if (!p) abort();
  return p;
}
__attribute__((returns_nonnull)) int *_Nullable nn(void); // expected-error {{can switch off Safe C's checks}}
__attribute__((no_thread_safety_analysis)) void nts(void); // expected-error {{can switch off Safe C's checks}}
static _Atomic int sect __attribute__((section("__TEXT,__const"))) = 1; // expected-error {{can switch off Safe C's checks}}
#pragma weak zz = nn // expected-error {{can switch off Safe C's checks}}
void nop(struct s *s);
void chosen(struct s *_Exclusive s, int c) {
  (c ? del : nop)(s); // expected-error {{has different ownership or bounds annotations}}
}
void locals(int n) {
  extern int block_scope; // expected-error {{declare 'block_scope' at file scope}}
  int *_Nullable v[n];    // expected-error {{is not zeroed}}
  (void)v;
}
static int *_Static const table[2] = {&(int){1}, (int[]){2, 3}}; // expected-error 0+ {{}}
#endif
