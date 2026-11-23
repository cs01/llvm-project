// A null check on a __null_terminated pointer carries through
// __null_terminated_to_indexable(), which keeps the pointer value. Found
// porting jsmn's tests.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=plain %s
// plain-no-diagnostics

void use(const char *__bidi_indexable p);

void narrowed(const char *_Nullable __null_terminated s) {
  if (s != 0)
    use(__null_terminated_to_indexable(s));
}

void unchecked(const char *_Nullable __null_terminated s) {
  use(__null_terminated_to_indexable(s)); // expected-error {{passing nullable pointer to nonnull parameter}} expected-note {{add a null check}}
}
