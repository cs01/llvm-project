// Under -fsafe-c an __unsafe block or function with nothing Safe C would
// reject is a warning, like Rust's unused_unsafe: every __unsafe left in the
// code is then needed.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

int needed(long x) {
  __unsafe { return *__unsafe_forge_single(int *, x); }
}

int not_needed(int a, int b) {
  __unsafe { return a + b; } // expected-warning {{unsafe block contains nothing Safe C would reject; remove '__unsafe'}}
}

__unsafe int fn_not_needed(int a) { return a; } // expected-warning {{unsafe function contains nothing Safe C would reject}}

__unsafe int fn_needed(int *_Nullable p) { return *p; }

int nested(long x) {
  __unsafe { // expected-warning {{unsafe block contains nothing}}
    __unsafe { return *__unsafe_forge_single(int *, x); }
  }
}

// A null dereference is reported by the end-of-function analysis, after the
// block is parsed.
int late(int *_Nullable p) {
  __unsafe { return *p; }
}
