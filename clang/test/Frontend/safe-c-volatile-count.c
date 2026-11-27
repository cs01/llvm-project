// A volatile count can change between the bounds check and the use, so no
// check built from it would hold; building one asserted. It is an error.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fbounds-safety -include safec.h -fsyntax-only -Xclang -verify %s
struct v { volatile int n; int *__counted_by(n) p; };
int f(void) {
  int a[4] = {0};
  struct v x = {4, a}; // expected-error {{the count of this pointer is read with side effects (it is 'volatile'), so it can change between the bounds check and the use; remove 'volatile'}}
  return x.p[0];
}
