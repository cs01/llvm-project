// Under -fsafe-c, `_Nullable __counted_by(n)` means __counted_by_or_null(n):
// null is allowed whatever the count, because nullability is checked on its
// own. Without _Nullable, __counted_by(n) stays non-null.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=plain %s

// expected-no-diagnostics
typedef __SIZE_TYPE__ size_t;
int sum1(const int *_Nullable __counted_by(n) v, size_t n);
int sum2(const int *__counted_by(n) _Nullable v, size_t n);
struct buf { char *_Nullable __counted_by(len) data; size_t len; };

void f(void) {
  (void)sum1(0, 4);
  (void)sum2(0, 4);
  struct buf b = {0, 8};
  (void)b; // plain-error@-1 {{initializing 'b.data'}}
}
// plain-error@15 {{passing null to parameter 'v'}}
// plain-error@16 {{passing null to parameter 'v'}}
