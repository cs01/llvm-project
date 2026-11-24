// Found by the Lean model of tagged unions (rule testKeepsUnwritten,
// 2026-11-24): after the tag changes, its member must be written before it is
// read, even if a tag test, a call that forgets the tag, or a join comes in
// between. A test narrows the tag; it does not write the member. The control
// run is plain C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

enum k { I, P };
struct T { enum k k; union _Tagged_by(k) { long i _When(I); const int *_Static p _When(P); }; };
void g(void);
void take(struct T *t);
static const int one = 1;

int after_test(void) {
  struct T t = {.k = I, .i = 0x41414141};
  t.k = P; // expected-error {{after this the tag}}
  if (t.k == P)
    return *t.p; // expected-error {{the tag 'k' now names 'p', but 'p' has not been written since the tag changed}}
  return 0;
}

int after_call(void) {
  struct T t = {.k = I, .i = 0x41414141};
  take(&t);
  t.k = P; // expected-error {{after this the tag}}
  g();
  if (t.k == P)
    return *t.p; // expected-error {{has not been written since the tag changed}}
  return 0;
}

int after_join(int c) {
  struct T t = {.k = I, .i = 0x41414141};
  if (c)
    t.k = P; // expected-error {{after this the tag}}
  if (t.k == P)
    return *t.p; // expected-error {{has not been written since the tag changed}}
  return 0;
}

int fine(struct T *in) {
  struct T t = *in;
  if (t.k == P)
    return *t.p;
  t.k = P;
  t.p = &one;
  g();
  if (t.k == P)
    return *t.p;
  return 0;
}
