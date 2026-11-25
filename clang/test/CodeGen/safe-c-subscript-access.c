// Every use of a[i] is checked, not only plain loads and stores. The increments
// and parenthesized stores wrote out of bounds silently before.
// RUN: %clang_cc1 -fsafe-c -fbounds-safety -w -O0 -triple arm64-apple-macosx -emit-llvm %s -o - | FileCheck %s
// CHECK-LABEL: define {{.*}} @f1(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f1(int *__attribute__((__counted_by__(n))) a, int n, int k) { a[k]++; return 0; }
// CHECK-LABEL: define {{.*}} @f2(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f2(int *__attribute__((__counted_by__(n))) a, int n, int k) { ++a[k]; return 0; }
// CHECK-LABEL: define {{.*}} @f3(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f3(int *__attribute__((__counted_by__(n))) a, int n, int k) { a[k]--; return 0; }
// CHECK-LABEL: define {{.*}} @f4(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f4(int *__attribute__((__counted_by__(n))) a, int n, int k) { --a[k]; return 0; }
// CHECK-LABEL: define {{.*}} @f5(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f5(int *__attribute__((__counted_by__(n))) a, int n, int k) { (a[k])++; return 0; }
// CHECK-LABEL: define {{.*}} @f6(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f6(int *__attribute__((__counted_by__(n))) a, int n, int k) { k[a]++; return 0; }
// CHECK-LABEL: define {{.*}} @f7(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f7(int *__attribute__((__counted_by__(n))) a, int n, int k) { (a[k]) = 1; return 0; }
// CHECK-LABEL: define {{.*}} @f8(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f8(int *__attribute__((__counted_by__(n))) a, int n, int k) { (a[k]) += 1; return 0; }
// CHECK-LABEL: define {{.*}} @f9(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f9(int *__attribute__((__counted_by__(n))) a, int n, int k) { return (a[k]); return 0; }
// CHECK-LABEL: define {{.*}} @f10(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f10(int *__attribute__((__counted_by__(n))) a, int n, int k) { if (!a[k]++) return 0; return 0; }
// CHECK-LABEL: define {{.*}} @f11(
// CHECK: call void @llvm.ubsantrap
// CHECK: ret i32
int f11(int *__attribute__((__counted_by__(n))) a, int n, int k) { _Generic(0, default: a[k])++; return 0; }
// One past the end is not an access.
// CHECK-LABEL: define {{.*}} @end(
// CHECK-NOT: icmp ult ptr
// CHECK: ret i32
int end(int *__attribute__((__counted_by__(n))) a, int n) { int *e = &a[n]; return e == a; }
