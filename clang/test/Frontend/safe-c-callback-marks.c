// A function is passed only where the function pointer carries the same marks.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -triple arm64-apple-macosx13 \
// RUN:   -mllvm -safe-c-redeclared=false -verify %s
#define _Owned [[clang::annotate_type("safec_owned")]]
char *_Owned _Nullable make(void) { return 0; }
int calls(char *_Nullable (*f)(void)) { return f() != 0; }
int f(void) {
  return calls(make); // expected-error {{different ownership or bounds annotations}}
}
