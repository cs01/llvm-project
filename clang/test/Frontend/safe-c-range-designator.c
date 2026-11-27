// '[a ... b] = x' evaluates x once and copies it into every element. With an
// owner or a call there, every element would share one allocation (a double
// free once each is freed), so only plain data with no side effects is allowed.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>

void *_Nullable make(void);
int g;

int calls(void) {
  int *_Owned _Nullable a[2] = {[0 ... 1] = malloc(4)}; // expected-error {{'[a ... b] = x' runs 'x' once and copies the result into every element, so an owner or a call there would be shared by all of them; use a constant, or set the elements in a loop}}
  free(_Take(a[0]));
  free(_Take(a[1]));
  return 0;
}

void copies(int *_Owned p) {
  int *_Owned _Nullable a[2] = {[0 ... 1] = p}; // expected-error {{'[a ... b] = x' runs 'x' once}}
  free(_Take(a[0]));
  free(_Take(a[1]));
}

int pointers(void) {
  int *b[2] = {[0 ... 1] = &g}; // expected-error {{'[a ... b] = x' runs 'x' once}}
  return *b[0];
}

int plain(int x) {
  int a[4] = {[0 ... 3] = x};
  int c[4] = {[0 ... 1] = 7, [2 ... 3] = 8};
  return a[0] + c[3];
}
