// Assigning a bounded pointer to a __null_terminated one suggests a fix-it;
// when the expression ends inside a macro expansion there is no place to
// insert it, and the fix-it merger crashed (found compiling lz4 under -fsafe-c).
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -verify %s
#include <ptrcheck.h>
#define KB *(1 << 10)

void f(const char *__counted_by(n) buf, int n) {
  const char *__null_terminated p;
  p = buf + 1 KB; // expected-error {{is an unsafe operation}}
  (void)p;
}
