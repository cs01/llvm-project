// Under -fsafe-c, nullability-safety violations are errors that neither
// -Wno-* flags nor diagnostic pragmas can silence.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -Wno-nullability-safety -Wno-error -verify %s
// Without -fsafe-c the same flags silence the warning entirely.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull \
// RUN:   -Wno-nullability-safety -verify=off %s
// off-no-diagnostics

#define NULL ((void *)0)

int deref(int *_Nullable p) {
  return *p; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check before dereferencing}}
}

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-safety"
int deref_under_pragma(int *_Nullable p) {
  return *p; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check before dereferencing}}
}
#pragma clang diagnostic pop

int *_Nonnull ret(int *_Nullable p __attribute__((lifetimebound))) {
  return p; // expected-error {{returning nullable pointer from function with nonnull return type}} expected-note {{add a null check before returning}}
}

int checked(int *_Nullable p) { return p ? *p : 0; }
