// A lifetimebound that the analysis cannot verify is reported at the
// parameter; __unsafe lifts it when every pointer return is inside __unsafe.
// Without M2 a pointer read from a field carries no borrow, so this getter is
// unverifiable. The control run checks that a return outside __unsafe still
// errors.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -DCONTROL -verify=control %s
// expected-no-diagnostics

struct T { int *_Owned _Nullable item; };
#ifndef CONTROL
int *_Nullable get(struct T *t _Lifetimebound) { __unsafe { return t->item; } }
#else
int *_Nullable get(struct T *t _Lifetimebound) { // control-error {{could not verify}}
  if (!t->item) { __unsafe { return t->item; } }
  return t->item;
}
#endif
