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
// CHECK: [[N:%.*]] = getelementptr inbounds nuw %struct.counted, ptr [[C]], i32 0, i32 1
// CHECK: store i64 0, ptr [[N]]
// CHECK: ret ptr [[V]]
