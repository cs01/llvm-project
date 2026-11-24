// M2 rule 9: a function returning a borrow (here, of an owned field) must
// mark the parameter it comes from __safec_lifetimebound, so callers keep the
// borrow tied to it. The control run is plain bounds safety, which does not
// run the lifetime analysis.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=off %s
// off-no-diagnostics

struct T { int *__safec_owned _Nullable item; };
int *_Nullable get(struct T *t) { return t->item; } // expected-error {{'get' returns a pointer borrowed from 't', so it is valid only as long as what 't' points to; mark the parameter '__safec_lifetimebound' so callers keep it alive, or return an owned copy}} expected-note {{param returned here}}
int *_Nullable get2(struct T *t __safec_lifetimebound) { return t->item; }
