// Under -fsafe-c a function that returns a pointer into a parameter must mark
// that parameter lifetimebound, so callers know the result dies with it.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s

struct obj { int v; int arr[4]; };

int *field_of(struct obj *o) { // expected-error {{'field_of' returns a pointer borrowed from 'o', so it is valid only as long as what 'o' points to; mark the parameter '_Lifetimebound'}}
  return &o->v;                // expected-note {{returned here}}
}

int *field_ok(struct obj *o _Lifetimebound) { return &o->v; }

int *caller(void) {
  struct obj local = {1, {0}};
  return field_ok(&local); // expected-error {{stack memory associated with local variable 'local' is returned}} expected-note {{returned here}} plain-warning {{address of stack memory}}
}
