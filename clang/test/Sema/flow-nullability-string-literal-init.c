// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c11 -verify %s
//
// The same in C, where a string literal has type char[N].

void take(const char *_Nonnull s);
const char *maybe(void);

void c_init(void) {
  const char *p = "abc";
  take(p);
}

void c_ternary(int c) {
  const char *p = c ? "yes" : "no";
  take(p);
}

char c_deref(void) {
  char *p = "abc";
  return *p;
}

// Still reported.

void c_nullable(void) {
  const char *p = maybe();
  take(p); // expected-warning {{passing nullable pointer to nonnull parameter 's'}} expected-note {{add a null check before the call}}
}
