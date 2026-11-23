// Under -fnullability-default=nonnull the pointee of an unannotated `int **`
// and the elements of an `int *[]` are nonnull: reads through them trust
// that, so a possibly-null store into them is reported.
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull -verify %s
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -verify=unspec %s
// unspec-no-diagnostics

#define NULL ((void *)0)

void through_alias(void) {
  int x = 1;
  int *data = &x;
  int **pp1 = &data;
  int **pp2 = &data;
  *pp1 = NULL; // expected-warning {{storing nullable pointer into the pointee of 'pp1', which is nonnull}} expected-note {{add a null check before storing}}
  int *q = *pp2;
  (void)*q;
}

void array_element(void) {
  int *arr[4];
  arr[2] = NULL; // expected-warning {{storing nullable pointer into an element of 'arr', which is nonnull}} expected-note {{add a null check before storing}}
}

void nullable_slots(int *_Nullable *pp, int *_Nullable *arr) {
  *pp = NULL;
  arr[1] = NULL;
}

void checked(int **pp, int *_Nullable v) {
  if (v)
    *pp = v;
}
