// Safe C integer semantics, as in Rust release builds: signed +, -, * wrap
// without a check; division by zero, INT_MIN / -1 and oversized shifts trap.
// RUN: %clang_cc1 -triple arm64-apple-macosx -fsafe-c -fwrapv \
// RUN:   -fsanitize=integer-divide-by-zero,signed-integer-overflow,shift-exponent \
// RUN:   -fsanitize-trap=integer-divide-by-zero,signed-integer-overflow,shift-exponent \
// RUN:   -O0 -emit-llvm -o - %s | FileCheck %s

// CHECK-LABEL: define{{.*}} i32 @add(
// CHECK: add i32
// CHECK-NOT: ubsantrap
// CHECK: ret i32
int add(int a, int b) { return a + b; }

// CHECK-LABEL: define{{.*}} i32 @mul(
// CHECK: mul i32
// CHECK-NOT: ubsantrap
// CHECK: ret i32
int mul(int a, int b) { return a * b; }

// Both the zero check and the INT_MIN / -1 check guard the division.
// CHECK-LABEL: define{{.*}} i32 @div(
// CHECK: icmp ne i32 %{{.*}}, 0
// CHECK: icmp ne i32 %{{.*}}, -2147483648
// CHECK: icmp ne i32 %{{.*}}, -1
// CHECK: call void @llvm.ubsantrap
// CHECK: sdiv i32
int div(int a, int b) { return a / b; }

// CHECK-LABEL: define{{.*}} i32 @shl(
// CHECK: icmp ule i32 %{{.*}}, 31
// CHECK: call void @llvm.ubsantrap
int shl(int a, int b) { return a << b; }
