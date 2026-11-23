// Under -fsafe-c, reading a local that may not be initialized is an error
// (Rust's definite initialization), and -Wno-uninitialized cannot turn the
// check off.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -Wno-uninitialized -verify %s
// Without -fsafe-c these are off-by-default warnings.
// RUN: %clang_cc1 -fsyntax-only -verify=plain %s
// plain-no-diagnostics

int definite(void) {
  int x;        // expected-note {{initialize the variable 'x' to silence this warning}}
  return x;     // expected-error {{variable 'x' is uninitialized when used here}}
}

int sometimes(int k) {
  int x;        // expected-note {{initialize the variable 'x' to silence this warning}}
  if (k)        // expected-error {{variable 'x' is used uninitialized whenever 'if' condition is false}} expected-note {{remove the 'if' if its condition is always true}}
    x = 1;
  return x;     // expected-note {{uninitialized use occurs here}}
}

int initialized(int k) {
  int x = 0;
  if (k)
    x = 1;
  return x;
}
