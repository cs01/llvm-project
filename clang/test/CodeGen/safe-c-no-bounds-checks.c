// `__unsafe _No_bounds_checks` compiles a function without run-time bounds
// checks (an audited hot loop); the same body without it keeps them. The
// attribute alone, without __unsafe, is an error (second RUN).
// RUN: %clang_cc1 -triple arm64-apple-macosx -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -O0 -emit-llvm -o - %s | FileCheck %s
// RUN: not %clang_cc1 -triple arm64-apple-macosx -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -DMISSING_UNSAFE -fsyntax-only %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ERR

int checked(int *__counted_by(n) a, int n, int i) { return a[i]; }
// CHECK-LABEL: define {{.*}} @checked(
// CHECK: call void @llvm.ubsantrap

/* The caller's loop keeps i < n. */
__unsafe _No_bounds_checks int unchecked(int *__counted_by(n) a, int n, int i) { return a[i]; }
// CHECK-LABEL: define {{.*}} @unchecked(
// CHECK-NOT: call void @llvm.ubsantrap
// CHECK: ret i32

#ifdef MISSING_UNSAFE
_No_bounds_checks int bad(int *__counted_by(n) a, int n, int i) { return a[i]; }
// ERR: error: 'bad' is '_No_bounds_checks', which compiles it without run-time bounds checks; that is a promise only an audited function can make, so mark it '__unsafe' too
#endif
