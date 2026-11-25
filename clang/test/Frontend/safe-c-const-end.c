// The end that strtol sets could write to text that must not change.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>
static const char table[4] = "12x";
void f(const char *s, char *__null_terminated w, int c, char *_Owned __null_terminated o) {
  char *__null_terminated _Nullable e = NULL;
  strtol("12x", &e, 10);              // expected-error {{'strtol' would set a pointer that can write to text that must not change}}
  strtol(s, &e, 10);                  // expected-error {{must not change}}
  strtod(("12x"), &e);                // expected-error {{must not change}}
  strtol(table, &e, 10);              // expected-error 1+{{}}
  strtol(c ? "1" : "2", &e, 10);      // expected-error {{must not change}}
  strtol(c ? s : w, &e, 10);          // expected-error {{must not change}}
  strtol(s + 1, &e, 10);              // expected-error {{must not change}}
  strtol(w, &e, 10);
  strtol(s, NULL, 10);
  strtol(s, 0, 10);
  free(o);
}
