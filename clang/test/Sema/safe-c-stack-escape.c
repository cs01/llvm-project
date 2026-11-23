// Under -fsafe-c the address of a local may be stored only in locals of the
// same function; through a pointer or in a global it outlives the local
// (audit probes r03 outp, r17 fill).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

struct S { int *_Nullable p; };
static int *_Nullable g; // expected-note {{this global dangles}}

void outp(int *_Nullable *out) {
  int y = 2;
  *out = &y; // expected-error {{storing the address of local 'y' through a pointer or in a global lets it outlive 'y'}}
}

void fill(struct S *s) {
  int x = 9;
  s->p = &x; // expected-error {{storing the address of local 'x'}}
}

void global(void) {
  int arr[4] = {0};
  g = arr; // expected-error {{storing the address of local 'arr'}} expected-error {{escapes to the global variable 'g'}}
}

int ok(void) {
  struct S arr[2];
  int y = 0;
  arr->p = &y; // a local array's element: still local
  int x = 1;
  int *p = &x;
  struct S local;
  local.p = &x;
  p = &x;
  return *p + (local.p ? *local.p : 0);
}
