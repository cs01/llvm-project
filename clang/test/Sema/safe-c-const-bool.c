// Under -fsafe-c a cast may not remove const (audit probe q12: a write to a
// const global), and bytes cannot be reinterpreted as _Bool, whose only valid
// values are 0 and 1 (probe q05: an index the optimizer assumed was 0 or 1).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify -verify-ignore-unexpected=note %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
static const int k = 5;
struct Flags { _Bool on; int n; };

void f(const char *__null_terminated s, int *raw, struct Flags *fl, int *const *pp) {
  int *p = (int *)&k;                 // expected-error {{removes 'const', so memory that must not change}}
  char *w = (char *)s;                // expected-error {{removes 'const'}}
  int **q = (int **)pp;               // expected-error {{removes 'const'}} expected-error {{can treat memory as the wrong type}} expected-error {{points to a place that holds a borrow}}
  const int *ok = (const int *)raw;   // adding const is fine
  _Bool *b = (_Bool *)raw;            // expected-error {{can treat memory as the wrong type}}
  memcpy(fl, raw, sizeof *raw);       // expected-error {{copied or cleared only whole}}
  (void)p; (void)w; (void)q; (void)ok; (void)b;
}
