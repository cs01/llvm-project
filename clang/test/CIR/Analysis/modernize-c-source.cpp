// A C file compiled as C++ through an #include, and C language linkage, keep
// signatures C can read: no references, but pointer to const is still C.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-export=ref.json a.cpp -o /dev/null
// RUN: FileCheck --check-prefix=REF --input-file=ref.json %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-export=const.json a.cpp -o /dev/null
// RUN: FileCheck --check-prefix=CONST --input-file=const.json %s

// REF:      "function": "inC",
// REF:      "blocked": "declared in a C source file"
// REF:      "function": "linkageC",
// REF:      "blocked": "C language linkage"
// REF:      "function": "inCpp",
// REF-NOT:  "blocked"
// REF:      "function": "use",

// CONST:     "function": "inC",
// CONST-NOT: "blocked"

//--- lib.c
struct S { int v; };
static int inC(struct S *s) { return s->v; }

//--- a.cpp
#include "lib.c"
extern "C" int linkageC(S *s) { return s->v; }
static int inCpp(S *s) { return s->v; }
int use(S *s) { return inC(s) + linkageC(s) + inCpp(s); }
