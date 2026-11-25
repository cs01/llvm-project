// An argument that already has an error must not get the implicit checked
// null-terminated conversion (it crashed the constant evaluator).
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify -verify-ignore-unexpected=note %s
#include <ptrcheck.h>
int strncmp(const char *__null_terminated, const char *__null_terminated, unsigned long);
int ok(void);
struct B { const unsigned char *content; unsigned long offset; }; // expected-error {{holds a pointer nothing tracks}}
int f(struct B *b) { return ok() && strncmp((const char *)(b->content + b->offset), "ab", 2) == 0; } // expected-error {{pointer arithmetic on single pointer}}
