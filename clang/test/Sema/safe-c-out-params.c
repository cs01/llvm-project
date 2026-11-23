// Under -fsafe-c, passing &p while p may be null requires the parameter's
// pointee to be _Nullable: the callee may read *param. This also covers the
// out-parameter idiom, which must declare `T *_Nullable *`.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s
// Without -fsafe-c the nonnull default does not apply this rule.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull \
// RUN:   -verify=plain %s
// plain-no-diagnostics

#define NULL ((void *)0)

static void read_through(int **pp) { (void)**pp; }
static void out_param(int *_Nullable *out) { *out = NULL; }

void address_of_null(void) {
  int *data = NULL;
  read_through(&data); // expected-error {{passing the address of 'data', which may be null, to a parameter whose pointee is nonnull}} expected-note {{add a null check before the call}}
}

void through_function_pointer(void) {
  int *data = NULL;
  void (*f)(int **) = read_through;
  f(&data); // expected-error {{passing the address of 'data', which may be null, to a parameter whose pointee is nonnull}} expected-note {{add a null check before the call}}
}

void declared_out_param(void) {
  int *data = NULL;
  out_param(&data);
}

void address_of_nonnull(void) {
  int x = 0;
  int *data = &x;
  read_through(&data);
}
