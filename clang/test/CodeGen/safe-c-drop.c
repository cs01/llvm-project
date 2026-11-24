// _Drop calls the cleanup at the end of the scope; _Take of a local leaves
// null for it. A local pointer is wide (it carries its bounds), so _Take
// copies it out whole, into the variable it initializes, then zeroes it.
// RUN: %clang_cc1 -triple arm64-apple-macosx -emit-llvm -o - -fsafe-c -fbounds-safety \
// RUN:   -include safec.h %s | FileCheck %s
typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
void keep(int *_Nullable _Owned p);
void f(void) {
  int *_Nullable p _Drop_free = malloc(4);
  keep(_Take(p));
}
// CHECK-LABEL: define {{.*}} @f(
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %{{.*}}, ptr align 8 %p, i64 24,
// CHECK-NEXT: call void @llvm.memset.p0.i64(ptr align 8 %p, i8 0, i64 24,
// CHECK: call void @keep(
// CHECK: call void @safec_drop_free_(ptr {{.*}}%p)
int g(void) {
  int *_Nullable p _Drop_free = malloc(8);
  int *_Nullable q = _Take(p);
  int v = q ? q[1] : 0;
  free(q);
  return v;
}
// CHECK-LABEL: define {{.*}} @g(
// CHECK: call void @llvm.memcpy.p0.p0.i64(ptr align 8 %q, ptr align 8 %p, i64 24,
// CHECK-NEXT: call void @llvm.memset.p0.i64(ptr align 8 %p, i8 0, i64 24,
