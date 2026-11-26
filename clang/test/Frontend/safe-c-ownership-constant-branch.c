// A branch on a constant goes one way: 'do { } while (0)' runs once.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
void once(void) { int *_Owned _Nullable p = malloc(4); do { free(p); } while (0); }
void dead(void) { int *_Owned _Nullable p = malloc(4); if (0) free(p); free(p); }
void loop(int c) {
  int *_Owned _Nullable p = malloc(4);
  do { free(p); } while (c); // expected-error {{'p' is freed after it was freed}}
}
void forever(void) {
  int *_Owned _Nullable p = malloc(4);
  while (1) { free(p); } // expected-error {{'p' is freed after it was freed}}
}
