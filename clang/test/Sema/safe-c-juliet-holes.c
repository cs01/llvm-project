// Found running more Juliet classes (2026-11-24): an _Owned member of an
// untagged union read through another member (double free), wide formats
// (no compiler checks them), and realloc's unzeroed growth read as pointers.
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
typedef __WCHAR_TYPE__ wchar_t;
void *_Nullable __sized_by_or_null(n) malloc(size_t n);
void *_Nullable __sized_by_or_null(n) realloc(void *_Nullable p, size_t n);
void free(void *_Nullable);
int wprintf(const wchar_t *__null_terminated f, ...);
union un { char *_Owned _Nullable a; char *_Owned _Nullable b; }; // expected-error {{'a' is an '_Owned' member of an untagged union}} expected-error {{'b' is an '_Owned' member}}
union one { char *_Owned _Nullable a; };
struct P { const char *_Static s; };
void wide(const wchar_t *__null_terminated f) {
  wprintf(f); // expected-error {{'wprintf' is not checked}}
}
void *_Nullable aligned_alloc(size_t a, size_t n);
 __attribute__((alloc_size(2))) void *_Nullable wrap(void *_Nullable _Owned p, size_t n) {
  return realloc(p, n); // expected-error {{'wrap' is marked 'alloc_size'}}
}
void other(void) {
  struct P *_Nullable p = realloc(0, sizeof(struct P)); // expected-error {{the memory 'realloc' adds is not zeroed}}
  free(p);
  struct P *_Nullable q = aligned_alloc(16, 160); // expected-error {{the memory 'aligned_alloc' returns is not zeroed}}
  free(q);
}
void grow(void) {
  struct P *_Nullable p = malloc(sizeof(struct P));
  struct P *_Nullable q = realloc(p, 500 * sizeof(struct P)); // expected-error {{'struct P' holds pointers, and the memory 'realloc' adds is not zeroed}}
  free(q);
  int *_Nullable a = malloc(sizeof(int));
  int *_Nullable b = realloc(a, 4 * sizeof(int));
  free(b);
}
