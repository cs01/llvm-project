// RUN: %clang_cc1 -triple arm64-apple-macosx14 -fclangir -emit-llvm %s -o - | FileCheck %s
// RUN: %clang_cc1 -triple arm64-apple-macosx14 -emit-llvm %s -o - | FileCheck %s

// Apple ARM64 keeps only the low 32 bits of a virtual member function
// pointer's vtable offset.

struct A {
  virtual int f();
};
int call(A *a, int (A::*m)()) { return (a->*m)(); }

// CHECK-LABEL: define {{.*}}@_Z4callP1AMS_FivE(
// CHECK: [[T:%.*]] = trunc i64 {{%.*}} to i32
// CHECK: [[Z:%.*]] = zext i32 [[T]] to i64
// CHECK: getelementptr {{.*}}i8, ptr {{%.*}}, i64 [[Z]]
