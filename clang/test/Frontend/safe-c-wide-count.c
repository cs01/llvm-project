// A constant count wider than 64 bits (__int128, _BitInt(70)) passed to a
// __counted_by parameter crashed bounds-safety's constant check, which read it
// as a uint64_t. It is now checked at run time, like a count not known until then.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fbounds-safety -include safec.h -fsyntax-only -Xclang -verify %s
// expected-no-diagnostics
int f(int *__counted_by(n) p, __int128 n);
int g(int *__counted_by(n) p, _BitInt(70) n);
int calls(void) {
  int a[4] = {0};
  return f(a, ((__int128)1 << 64) + 4) + g(a, ((_BitInt(70))1 << 64) + 4);
}
