// A C translation unit lists the files it includes, so project mode keeps
// C++ syntax out of a header C also compiles. A C++ one lists none.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-export=c.json a.c -o /dev/null
// RUN: FileCheck --input-file=c.json %s
// RUN: %clang_cc1 -x c++ -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-export=cxx.json a.c -o /dev/null
// RUN: FileCheck --check-prefix=CXX --input-file=cxx.json %s

// CHECK: "cFiles": [
// CHECK-DAG: shared.h"
// CHECK-DAG: a.c"
// CXX-NOT: "cFiles"

//--- shared.h
static inline int first(const int *p) { return *p; }
//--- a.c
#include "shared.h"
int use(int *p) { return first(p); }
