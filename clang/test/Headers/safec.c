// safec.h lets annotated Safe C code build as plain C on any compiler.

// A vendored copy alone, with no ptrcheck.h and no Clang keywords (as with
// GCC): every annotation expands to nothing and the file compiles cleanly.
// RUN: rm -rf %t && mkdir -p %t && cp %S/../../lib/Headers/safec.h %t/
// RUN: %clang_cc1 -fsyntax-only -nostdsysteminc -nobuiltininc -I %t -U__clang__ -verify=portable %s
// portable-no-diagnostics

// The same source under -fsafe-c is checked.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -verify=safe %s

#include <safec.h>
typedef __SIZE_TYPE__ size_t;

int sum(const int *__counted_by(n) a, size_t n) {
  int s = 0;
  for (size_t i = 0; i < n; i++)
    s += a[i];
  return s;
}

int first(int *_Nullable p) {
  return *p; // safe-error {{dereference of nullable pointer}} safe-note {{add a null check}}
}

int escape(int *_Nullable p) {
  __unsafe { return *p; }
}
