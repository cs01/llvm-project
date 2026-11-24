// The SDK leaves wcscat's destination unannotated, so it could write past
// it unchecked; Safe C rejects it and points at the bounded form. With the
// real SDK headers (Sema tests cannot use them). Control: plain clang.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsyntax-only -Xclang -verify=plain %s
// plain-no-diagnostics
#include <wchar.h>
size_t f(const wchar_t *s) {
  wchar_t d[4] = {0};
  wcscat(d, s); // expected-error {{'wcscat' can write past the end of its destination, which nothing checks; use the size-bounded form, as in 'wcslcat(dst, src, N)' with N the destination's length in wide characters}}
  return wcslcat(d, s, sizeof d / sizeof *d);
}
