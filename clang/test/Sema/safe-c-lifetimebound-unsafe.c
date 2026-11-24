// A lifetimebound that the analysis cannot verify is reported at the
// parameter; __unsafe lifts it when every pointer return is inside __unsafe.
// Here the getter returns a borrow of an owned field through a path the
// analysis cannot follow (a cast). The control run checks that a return
// outside __unsafe still errors.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -DCONTROL -verify=control %s
// expected-no-diagnostics

struct T { int *_Owned _Nullable item; };
#ifndef CONTROL
int *_Nullable _Static g;
int *_Nullable get(struct T *t _Lifetimebound) { __unsafe { return g; } }
#else
int *_Nullable _Static g;
int *_Nullable get(struct T *t _Lifetimebound) { // control-error {{could not verify}}
  if (!t->item) { __unsafe { return g; } }
  return g;
}
#endif
