// Under -fsafe-c, passing a pointer that carries bounds to a __null_terminated
// parameter inserts the checked conversion (scan for the terminator within the
// bounds, trap if none) instead of demanding __unsafe_null_terminated_from_indexable.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify -verify-ignore-unexpected=note %s
// Plain bounds safety still requires the explicit conversion.
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -verify=plain -verify-ignore-unexpected=note %s

#include <ptrcheck.h>
typedef __SIZE_TYPE__ size_t;
size_t my_strlen(const char *__null_terminated s __attribute__((noescape)));
struct S { const char *__null_terminated name; }; // expected-error {{holds a pointer nothing tracks}}

size_t arg(void) {
  char buf[16] = "hello";
  return my_strlen(buf); // plain-error {{is an unsafe operation}}
}

void field(struct S *s) {
  char buf[16] = "hello";
  s->name = buf; // expected-error {{is an unsafe operation}} expected-error {{storing the address of local 'buf'}} plain-error {{is an unsafe operation}}
}
