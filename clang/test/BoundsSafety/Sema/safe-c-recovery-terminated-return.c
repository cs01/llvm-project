// Returning an invalid pointer expression from a function with a
// null-terminated result type must not crash after the error.
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include ptrcheck.h -verify %s

typedef struct { const unsigned char *json; unsigned long position; } E; // expected-note {{pointer 'E::json' declared here}}
static E g = {0, 0};
const char *__null_terminated f(void) {
  return (const char *)(g.json + g.position); // expected-error {{pointer arithmetic on single pointer 'g.json' is out of bounds}}
}
