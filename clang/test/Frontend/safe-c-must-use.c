// A result type, or a function marked to be checked, is used or cast to
// (void); no flag lowers it.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -fsyntax-only -w -Wno-everything -Wno-error -Xclang -verify %s
#include <stdlib.h>
struct __attribute__((warn_unused_result)) result { int ok; };
struct result try_it(void);
void f(void *_Owned _Nullable p) {
  try_it();                // expected-error {{ignoring return value of type 'result'}}
  (void)try_it();
  if (try_it().ok) {}
  realloc(p, 4);           // expected-error {{ignoring return value of function declared with 'warn_unused_result'}}
}
