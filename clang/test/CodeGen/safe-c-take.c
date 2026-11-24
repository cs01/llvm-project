// _Take(lv) loads the pointer, stores null, and zeroes the count field
// of a __counted_by pointer, so the struct's bounds stay true. The struct
// pointer is evaluated once. Safe C's kind annotations emit nothing.
// RUN: %clang_cc1 -triple arm64-apple-macosx -emit-llvm -o - -fsafe-c -fbounds-safety \
// RUN:   -include safec.h %s | FileCheck %s
typedef __SIZE_TYPE__ size_t;
struct counted { int *_Nullable _Owned __counted_by(n) data; size_t n; };
struct counted *next(void);
int *_Nullable take(void) { return _Take(next()->data); }
// CHECK-LABEL: define {{.*}} @take(
// CHECK: [[C:%.*]] = call ptr @next()
// CHECK-NOT: @next
// CHECK-NOT: annotation
// CHECK: [[DATA:%.*]] = getelementptr inbounds nuw %struct.counted, ptr [[C]], i32 0, i32 0
// CHECK: [[V:%.*]] = load ptr, ptr [[DATA]]
// CHECK: store ptr null, ptr [[DATA]]
// CHECK: [[N:%.*]] = getelementptr inbounds nuw %struct.counted, ptr {{.*}}, i32 0, i32 1
// CHECK: store i64 0, ptr [[N]]
// CHECK: ret ptr [[V]]

// A local pointer to the struct is a wide (bounds-carrying) pointer, and an
// array element has no pointer at all; both used to crash.
_Returns_owned struct counted *_Nullable pick(void);
int *_Nullable take_local(void) {
  struct counted *_Nullable p = pick();
  if (!p)
    return 0;
  return __safec_take(p->data);
}
// CHECK-LABEL: define {{.*}} @take_local(
// CHECK: store ptr null
// CHECK: store i64 0
int *_Nullable take_elem(struct counted *_Excl __counted_by(2) a) {
  return __safec_take(a[1].data);
}
// CHECK-LABEL: define {{.*}} @take_elem(
// CHECK: store ptr null
// CHECK: store i64 0
