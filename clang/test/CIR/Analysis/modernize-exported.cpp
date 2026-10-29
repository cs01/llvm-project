// A function with an export attribute, or written with an export macro, is
// part of the library's API, and its candidates say so. An export macro
// counts even when this build defines it empty: the shared build, in a branch
// this one skips, makes it visibility("default").

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-export=%t.json %s -o /dev/null
// RUN: FileCheck --input-file=%t.json %s

#if defined(SHARED_LIBRARY)
#define MY_EXPORT __attribute__((visibility("default")))
#else
#define MY_EXPORT
#endif

// CHECK:      "function": "byMacro",
// CHECK:      "exported": true
MY_EXPORT int byMacro(int *p) { return *p; }

// CHECK:      "function": "byAttribute",
// CHECK:      "exported": true
__attribute__((visibility("default"))) int byAttribute(int *p) { return *p; }

// CHECK:      "function": "Api::get",
// CHECK:      "exported": true
class MY_EXPORT Api {
public:
  int get(int *p);
};
int Api::get(int *p) { return *p; }

// CHECK:      "function": "plain",
// CHECK-NOT:  "exported"
// CHECK:      "function"
int plain(int *p) { return *p; }
int after(int *p) { return *p; }
