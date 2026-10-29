// In project mode a function a macro writes is edited in that macro: every
// function the macro writes gets the same edit, which project.ts keeps
// together. A type macro other declarations share is not edited.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-export=a.json a.cpp -o /dev/null
// RUN: FileCheck --input-file=a.json %s

// CHECK:      "function": "read_int",
// CHECK-NOT:  "blocked"
// CHECK:      "offset": [[OFF:[0-9]+]],
// CHECK-NEXT: "length": 0,
// CHECK-NEXT: "text": "const "
// CHECK:      "function": "read_long",
// CHECK-NOT:  "blocked"
// CHECK:      "offset": [[OFF]],
// CHECK:      "function": "peek_short",
// CHECK-NOT:  "blocked"
// CHECK:      "text": " const "
// CHECK:      "function": "viaShared",
// CHECK-NEXT: "param": "p",
// CHECK:      "blocked": "const: pointer spelled through a typedef or macro"

//--- a.cpp
#define GEN_READ(T) \
  static T read_##T(T *p) { return *p; }
GEN_READ(int)
GEN_READ(long)
#define GEN_PEEK(T) \
  static T peek_##T(T* p) { return *p; }
GEN_PEEK(short)
#define INTP int *
static int viaShared(INTP p) { return *p; }
int use(int *i, long *l, short *s) {
  return read_int(i) + (int)read_long(l) + viaShared(i) + peek_short(s);
}
