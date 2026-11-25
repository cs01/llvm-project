// Blind attack, group 2 (2026-11-24): function pointers keep a function's
// promises, library functions as pointers, blocks, and tags through pointers.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -mllvm -safe-c-strict-ownership=true -fblocks -fsyntax-only -Xclang -verify %s
#include <safec.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
void count(int *__counted_by(n) p, int n) { if (n) p[n - 1] = 7; }
struct s { int *_Owned _Nullable p; };
void del(struct s *_Exclusive s) { free(_Take(s->p)); }
int *id(int *a) { return a; }
struct ops { void (*f)(int *, int); };
void call(void (*f)(struct s *)) { (void)f; }
void pointers(void) {
  void (*g)(int *, int) = count;          // expected-error {{parameter 1 of this function has different}}
  g = count;                              // expected-error {{parameter 1 of this function has different}}
  struct ops o = {count};                 // expected-error {{parameter 1 of this function has different}}
  call(del);                              // expected-error {{parameter 1 of this function has different}}
  void (*ok)(int *__counted_by(n) p, int n) = count;
  void (*ok2)(struct s *_Exclusive) = del;
  (void)g; (void)o; (void)ok; (void)ok2;
}
int through(void) {
  int *(*f)(int *) = id;
  int *q;
  { int x = 5; q = f(&x); } // expected-error 1+ {{does not live long enough}} expected-note 0+ {{}}
  return *q;                // expected-note 1+ {{later used here}}
}
void library(void) {
  int (*p)(const char *, ...) = printf; // expected-error {{'printf' is checked only when called by name}}
  int (*c)(int) = abs;
  (void)p; (void)c;
}
void block(int *p) {
  void (^b)(void) = ^{ *p = 3; }; // expected-error {{blocks are not checked}} expected-error {{no rules for this kind of type}}
  b(); // expected-error {{blocks are not checked}}
}
struct t { int k; union _Tagged_by(k) { const char *_Static p _When(1); long l _When(0); }; };
void tag(void) {
  struct t t = {.k = 0, .l = 77};
  int *q = &t.k; // expected-error {{'k' is a tag}}
  *q = 1;
  t.k++;         // expected-error {{'k' is a tag}}
}
