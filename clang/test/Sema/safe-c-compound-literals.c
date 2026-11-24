// Fuzzing hole 02 (2026-11-23): a block-scope compound literal is unnamed
// local storage. Returning its address, or assigning its address to a
// pointer that may outlive the block, is rejected; initializing a local
// with it (same block) and passing it to a call are fine. The control run
// is plain C without Safe C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -verify=off %s

int *f(int v) {
  return (int[]){v, v}; // expected-error {{stack memory associated with compound literal is returned}} expected-note {{returned here}} off-warning {{address of stack memory associated with compound literal}}
}

int *g(void) {
  int *p = (int[]){1, 2}; // expected-error {{stack memory associated with compound literal is returned}}
  return p;               // expected-note {{returned here}}
}

int block(void) {
  int *p = 0;
  {
    p = (int[]){1, 2, 3}; // expected-error {{this compound literal's storage ends with the block it is written in, and the pointer assigned here may outlive it; declare it as a named variable in the block where the pointer is used (as in 'int vals[] = {1, 2};'), or initialize a new pointer with it}}
  }
  return p ? p[1] : 0;
}

struct pt { int x; };
int use(const struct pt *q);
int ok(void) {
  int *p = (int[]){1, 2};
  struct pt v = (struct pt){4};
  return p[1] + use(&(struct pt){3}) + v.x;
}
