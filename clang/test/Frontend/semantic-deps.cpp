// RUN: rm -rf %t && split-file %s %t
// RUN: %clang_cc1 -std=c++17 -x c++-header %t/lib.h -emit-pch -o %t/lib.pch
// RUN: %clang_cc1 -std=c++17 -include-pch %t/lib.pch -fsemantic-deps \
// RUN:   -emit-obj %t/use.cpp -o %t/use.o
// RUN: FileCheck %s --input-file=%t/use.o.sdeps
// RUN: FileCheck %s --check-prefix=NOT --input-file=%t/use.o.sdeps

// A member is recorded with its class, a function whose body was loaded from
// the PCH as "Class::name()", and a macro the compile expanded as "%NAME".
// CHECK-DAG: {{^}}Point::sum{{$}}
// CHECK-DAG: {{^}}twice{{$}}
// CHECK-DAG: {{^}}Point::sum(){{$}}
// CHECK-DAG: {{^}}twice(){{$}}
// CHECK-DAG: {{^}}%SCALE{{$}}
// CHECK-DAG: {{^}}#file {{.*}}use.cpp{{$}}

// What the compile never touched is not recorded, though the PCH has it.
// NOT-NOT: unused
// NOT-NOT: #file {{.*}}lib.h

//--- lib.h
#define SCALE 3
struct Point {
  int x, y;
  int sum() const { return x + y; }
  int unused() const { return 1; }
};
inline int twice(int v) { return 2 * v; }
inline int unusedFree(int v) { return v; }

//--- use.cpp
int f(const Point &p) { return p.sum() + twice(SCALE); }
