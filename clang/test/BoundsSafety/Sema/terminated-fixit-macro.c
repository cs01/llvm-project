// Assigning a bounded pointer to a __null_terminated one suggests a fix-it;
// when the expression ends inside a macro expansion there is no place to
// insert it, and the fix-it merger crashed (found compiling lz4 under -fsafe-c).
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify=expected,plain %s
// RUN: not %clang_cc1 -fsyntax-only -fbounds-safety -fdiagnostics-parseable-fixits %s 2>&1 | FileCheck %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -verify %s
#include <ptrcheck.h>
#define KB *(1 << 10)

void f(const char *__counted_by(n) buf, int n) {
  const char *__null_terminated p;
  p = buf + 1 KB; // expected-error {{is an unsafe operation}} plain-note 2 {{consider using '__unsafe_null_terminated_from_indexable()'}}
  (void)p;
}
// The closing parenthesis goes after the macro invocation, at column 17.
// CHECK: fix-it:"{{.*}}":{12:7-12:7}:"__unsafe_null_terminated_from_indexable("
// CHECK: fix-it:"{{.*}}":{12:17-12:17}:")"
