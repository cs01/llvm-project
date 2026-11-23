// Apple's __unsafe_ forms make up bounds or terminators the compiler cannot
// check; under -fsafe-c they are errors outside '__unsafe'. A read-only view
// that includes the terminator is allowed: nothing can erase it through it.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=plain %s
// plain-no-diagnostics

char *__null_terminated a(char *__counted_by(n) p, int n) {
  return __unsafe_null_terminated_from_indexable(p); // expected-error {{'__unsafe_terminated_by_from_indexable' can switch off Safe C's checks}}
}
char *__bidi_indexable b(char *__null_terminated s) {
  return __unsafe_null_terminated_to_indexable(s); // expected-error {{'__unsafe_terminated_by_to_indexable' can switch off}}
}
const char *__bidi_indexable c(const char *__null_terminated s) {
  return __unsafe_null_terminated_to_indexable(s);
}
int *d(long x) {
  return __unsafe_forge_single(int *, x); // expected-error {{'__unsafe_forge_single' can switch off}}
}
int *__bidi_indexable e(int *p) {
  return __unsafe_forge_bidi_indexable(int *, p, 400); // expected-error {{'__unsafe_forge_bidi_indexable' can switch off}}
}
char *__null_terminated f(char *p) {
  return __unsafe_forge_null_terminated(char *, p); // expected-error {{'__unsafe_forge_terminated_by' can switch off}}
}
int *g(long x) {
  __unsafe { return __unsafe_forge_single(int *, x); }
}
