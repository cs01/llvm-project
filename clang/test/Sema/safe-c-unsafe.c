// __unsafe (from safec.h) is the one escape from Safe C's rules: inside an
// __unsafe block or function they are not enforced; everywhere else they are.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s

#include <safec.h>
#define NULL ((void *)0)

int block(int *_Nullable p) {
  __unsafe {
    int x = *p;      // no error: the end-of-TU analysis is inside __unsafe
    int *_Nonnull q = NULL; // no error: raised while parsing __unsafe
    (void)q;
    if (x)
      return x;
  }
  return *p; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check}}
}

__unsafe int whole_function(int *_Nullable p) {
  int *_Nonnull q = NULL;
  (void)q;
  return *p;
}

int after(int *_Nullable p) {
  return *p; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int nested(int *_Nullable p, int k) {
  if (k) {
    __unsafe { return *p; }
  }
  return p ? *p : 0;
}
