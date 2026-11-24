// Blind attack, owners, tags, builtins and races (2026-11-24).
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -DRUN -O1 %s -o %t && not --crash %t
#include <safec.h>
#include <signal.h>
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
struct t { int k; union _Tagged_by(k) { const char *_Static p _When(1); long l _When(0); }; };
void tags(int c) {
  struct t t = {.k = c, .l = 77}; // expected-error {{known only at run time}}
  t.k = 1; // expected-error {{after this the tag 'k'}}
  (void)t;
}
struct t paired(void) {
  struct t t = {.k = 0, .l = 77};
  t.k = 1;
  t.p = "x";
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
#endif
