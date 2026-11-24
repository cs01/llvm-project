// Tagged-union holes found by the fuzzing pass (2026-11-23), each of which
// compiled and then read an integer as a pointer:
//  06 a tag write alone (the newly named member was never written)
//  07 a member reached through an array element (not followed: now an error)
//  12 an initializer naming a member the tag does not make live
//  14 a call that changes a global's tag
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -mllvm -safe-c-ownership=false -fsyntax-only -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -include safec.h -verify=off %s
// off-no-diagnostics

enum K { I, P };
struct T { enum K k; union _Tagged_by(k) { long i _When(I); int *_Static p _When(P); }; };
static int gx = 5;

int h06(void) {
  struct T t = {.k = I, .i = 0};
  t.i = 0x41414141;
  t.k = P;
  return *t.p; // expected-error {{the tag 'k' now names 'p', but 'p' has not been written since the tag changed, so the union still holds another member's bytes; write 'p' first}}
}
int h06_ok(void) {
  struct T t = {.k = I, .i = 0};
  t.k = P;
  t.p = &gx;
  return *t.p;
}

int h07(int j) {
  struct T a[2] = {{.k = P, .p = &gx}, {.k = P, .p = &gx}};
  if (a[0].k == P) {
    a[j].k = I;
    return *a[0].p; // expected-error {{'p' is reached through something whose tag cannot be followed here (an array element or a call result); take a pointer to the struct first}}
  }
  return 0;
}
int h07_ok(struct T *__counted_by(n) a, int n, int i) {
  if (i < 0 || i >= n)
    return 0;
  struct T *e = &a[i];
  return e->k == P ? *e->p : 0;
}

int h12(void) {
  struct T t = {.k = P, .i = 0x41414140}; // expected-error {{this initializer sets the tag to 1 but fills 'i', which is not live for that tag; initialize the member the tag names}}
  return t.k == P ? *t.p : 0;
}

static struct T TG;
static void rt(void) { TG.k = I; TG.i = 0x41414140; }
int h14(void) {
  TG.k = P;
  TG.p = &gx;
  if (TG.k == P) {
    rt();
    return *TG.p; // expected-error {{reading 'p' needs the tag 'k' known to be P here}}
  }
  return 0;
}
