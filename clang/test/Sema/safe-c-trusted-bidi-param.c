// A trusted header's parameter that carries its own bounds (__bidi_indexable)
// is not treated as a terminated string: no terminator scan, and no crash
// when the header itself calls it. The control run is plain bounds safety.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -isystem %S/Inputs -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -isystem %S/Inputs -verify %s
// expected-no-diagnostics
#include <safe-c-trusted-bidi.h>
char g(void) {
  char buf[4] = {'a', 'b', 'c', 'd'};
  return first_via(buf);
}
