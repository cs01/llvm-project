// RUN: rm -rf %t && split-file %s %t
// RUN: sed -e "s|DIR|%/t|g" %t/cdb.json.in > %t/compile_commands.json
// RUN: clang-semindex -p %t --header %t/lib.h %t/use.cpp > %t/index.json
// RUN: FileCheck %s --input-file=%t/index.json

// B's tokens do not change when A does, so B refers to A in the graph.
// CHECK:      "B": {
// CHECK-NEXT:   "names": [
// CHECK-NEXT:     "B"
// CHECK-NEXT:   ],
// CHECK-NEXT:   "refs": [
// CHECK-NEXT:     "A"
// CHECK-NEXT:   ]

// A body is its own node, and so is what it refers to.
// CHECK:      "S::get()": {
// CHECK-NEXT:   "body": "S::get",
// CHECK:        "refs": [
// CHECK-NEXT:     "B"

// Through a pointer only the existence of a type matters.
// CHECK:      "useOpaque": {
// CHECK:        "refs": [
// CHECK-NEXT:     "&Opaque"

// Entities refer to macro definitions they expand, by definition.
// CHECK:      "useScale": {
// CHECK:        "refs": [
// CHECK-NEXT:     "%SCALE@{{.*}}lib.h#0"

//--- lib.h
#define SCALE 3
constexpr int A = 1;
constexpr int B = A + 1;
struct S {
  int get() const { return B * 3; }
};
struct Opaque;
int useOpaque(Opaque *o);
inline int useScale() { return SCALE; }

//--- use.cpp
int f() { return S().get(); }

//--- cdb.json.in
[{"directory": "DIR", "file": "DIR/use.cpp", "command": "clang++ -std=c++17 -c DIR/use.cpp"}]
