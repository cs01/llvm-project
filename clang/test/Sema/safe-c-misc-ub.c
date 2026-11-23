// Under -fsafe-c: falling off the end of a non-void function is an error, and
// string literals are const, so dropping const to write one is an error.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -fconst-strings -verify -verify-ignore-unexpected=note %s
// RUN: %clang_cc1 -fsyntax-only -verify=plain -Wno-return-type %s
// plain-no-diagnostics

int g;
int *pick(int c) {
  if (c > 5)
    return &g;
} // expected-error {{non-void function does not return a value in all control paths}}

void write_literal(void) {
  char *s = "hello"; // expected-error {{discards qualifiers}}
  (void)s;
}
