// With bounds safety, realloc's result gets internal casts for its bounds
// (e.g. `(char *)result + size`); they still count as a fresh allocation.
// RUN: %clang_cc1 -mllvm -safe-c-redeclared=false -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s
// expected-no-diagnostics

#include <safec.h>
typedef __SIZE_TYPE__ size_t;
void *__sized_by_or_null(n) realloc(void *_Nullable p, size_t n);

 int *_Nullable grow(int *_Nullable old, size_t n) {
  int *_Nullable p = realloc(old, n * sizeof(int));
  return p;
}
