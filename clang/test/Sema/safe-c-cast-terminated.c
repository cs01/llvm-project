// Under -fsafe-c a terminated string cast to another character type stays
// terminated, so indexing it is checked against the terminator instead of
// trapping as a one-character pointer. Found porting cJSON.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

unsigned scan(const unsigned char *__null_terminated u);

unsigned f(const char *__null_terminated s) {
  const unsigned char *u = (const unsigned char *)s;
  unsigned n = 0;
  while (*u) {
    n += *u;
    u++;
  }
  return n + scan((const unsigned char *)s);
}

// Not characters: no terminated view of a wider type.
const int *g(const char *__null_terminated s _Lifetimebound) {
  return (const int *)s; // expected-error {{can treat memory as the wrong type}} expected-note {{memcpy them into a variable of the new type}} expected-error {{needs stricter alignment}}
}
