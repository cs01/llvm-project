// Two files that disagree about a type reached from a shared symbol do not
// link: through a field, a pointer to array, a function pointer, packing, a
// terminator, thread-local storage (third blind attack, two-file holes).
// RUN: split-file %s %t
// RUN: %clang_cc1 -fsafe-c -fbounds-safety -triple arm64-apple-macosx13 -emit-llvm %t/a.c -o - | grep -o 'type_differs[^"]*' | sort -u > %t/a
// RUN: %clang_cc1 -fsafe-c -fbounds-safety -triple arm64-apple-macosx13 -emit-llvm %t/same.c -o - | grep -o 'type_differs[^"]*' | sort -u > %t/same
// RUN: %clang_cc1 -fsafe-c -fbounds-safety -triple arm64-apple-macosx13 -emit-llvm %t/b.c -o - | grep -o 'type_differs[^"]*' | sort -u > %t/b
// RUN: diff %t/a %t/same
// RUN: not diff %t/a %t/b > %t/d
// RUN: FileCheck %s < %t/d
// CHECK-DAG: ! {{.*}}f1.struct.T.
// CHECK-DAG: ! {{.*}}f2.struct.T.
// CHECK-DAG: ! {{.*}}f3.struct.T.
// CHECK-DAG: ! {{.*}}f4.struct.P.
// CHECK-DAG: ! {{.*}}f5.{{[0-9A-F]+}}
// CHECK-DAG: ! {{.*}}f6.{{[0-9A-F]+}}
// CHECK-DAG: ! {{.*}}tls.{{[0-9A-F]+}}
//--- a.c
struct T { long a; };
struct S { struct T *t; };
#pragma pack(1)
struct P { char c; long a; };
#pragma pack()
int f1(struct S *s) { return 0; }
int f2(struct T (*s)[1]) { return 0; }
int f3(int (*g)(struct T *)) { return 0; }
int f4(struct P *p) { return 0; }
int f5(int (*c)(int *__attribute__((__counted_by__(3))))) { return 0; }
int f6(const char *__attribute__((__terminated_by__('/'))) p) { return 0; }
_Thread_local int tls;
//--- same.c
struct T { long a; };
struct S { struct T *t; };
#pragma pack(1)
struct P { char c; long a; };
#pragma pack()
int f1(struct S *renamed) { return 1; }
int f2(struct T (*s)[1]) { return 0; }
int f3(int (*g)(struct T *)) { return 0; }
int f4(struct P *p) { return 0; }
int f5(int (*c)(int *__attribute__((__counted_by__(3))))) { return 0; }
int f6(const char *__attribute__((__terminated_by__('/'))) p) { return 0; }
_Thread_local int tls;
//--- b.c
struct T { int *a; };
struct S { struct T *t; };
struct P { char c; long a; };
int f1(struct S *s) { return 0; }
int f2(struct T (*s)[1]) { return 0; }
int f3(int (*g)(struct T *)) { return 0; }
int f4(struct P *p) { return 0; }
int f5(int (*c)(int *)) { return 0; }
int f6(const char *__attribute__((__terminated_by__('.'))) p) { return 0; }
int tls;
