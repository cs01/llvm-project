// Under -fsafe-c, variadic calls are checked through their format strings:
// mismatches, non-literal formats and unbounded scanf %s/%[ are errors, as
// are calls through declarations without a prototype.
// RUN: %clang_cc1 -fsyntax-only -std=gnu17 -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify -verify-ignore-unexpected=note %s
// RUN: %clang_cc1 -fsyntax-only -std=gnu17 -Wno-format -Wno-deprecated-non-prototype -verify=plain %s
// plain-no-diagnostics

int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int sscanf(const char *, const char *, ...) __attribute__((format(scanf, 2, 3)));

void mismatch(long v) {
  printf("%s\n", v); // expected-error {{format specifies type 'char *' but the argument has type 'long'}}
}

void unbounded(const char *in) {
  char buf[4];
  sscanf(in, "%s", buf);   // expected-error {{'%s' without a field width can write past the end of the buffer}}
  sscanf(in, "%3s", buf);  // ok: bounded
  sscanf(in, "%[a-z]", buf); // expected-error {{'%[' without a field width can write past the end of the buffer}}
}

int old();
int call_old(void) {
  return old(1); // expected-error {{passing arguments to 'old' without a prototype is deprecated}}
}
