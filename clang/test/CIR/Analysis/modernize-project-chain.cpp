// In project mode a chain into a function another translation unit defines
// is left open: the caller is proposed with a need on the callee, and
// project.ts settles it once every translation unit is in.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-export=b.json b.cpp -o /dev/null
// RUN: FileCheck --input-file=b.json %s

// CHECK:      "function": "caller",
// CHECK-NOT:  "blocked"
// CHECK:      "needs": [
// CHECK-NEXT:   {
// CHECK-NEXT:     "usr": "c:@F@callee#*I#",
// CHECK-NEXT:     "index": 0,
// CHECK-NEXT:     "kind": "const"

//--- b.cpp
int callee(int *p);
int caller(int *q) { return callee(q); }
