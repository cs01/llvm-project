// A union holding a struct that ends in a counted flexible array is marked as
// having a flexible array itself; initializing the union through its other
// member asserted in the count check. Safe C rejects the nesting (the count of
// a nested flexible array is not checked); plain bounds-safety compiles it.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fbounds-safety -include safec.h -fsyntax-only -Xclang -verify=plain %s
// plain-no-diagnostics
struct F { int n; int d[__counted_by(n)]; };
union U { struct F f; int a[4]; }; // expected-error {{'struct F' ends in an array whose length is a field}}
int g(void) {
  union U u = {.a = {100}};
  return u.a[0];
}
