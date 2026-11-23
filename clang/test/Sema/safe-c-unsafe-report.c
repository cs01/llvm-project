// -Rsafe-c-unsafe lists every __unsafe region, including one that a macro
// expands to, so unsafe code hidden behind a library macro stays visible.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -Rsafe-c-unsafe -verify %s
// Without the flag, nothing is printed.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify=quiet %s
// quiet-no-diagnostics

#include <safec.h>

#define DEREF(p) ({ int r_; __unsafe { r_ = *(p); } r_; })

int block(int *_Nullable p) {
  __unsafe { return *p; } // expected-remark {{unsafe block: Safe C rules are not enforced here}}
}

__unsafe int whole(int *_Nullable p) { return *p; } // expected-remark {{unsafe function: Safe C rules are not enforced here}}

int via_macro(int *_Nullable p) {
  return DEREF(p); // expected-remark {{unsafe block: Safe C rules are not enforced here}}
}
