// Under -fsafe-c, two files that declare one external symbol with different
// types do not link (audit probes w10, v05: int tbl[1] defined, extern int
// tbl[1000] used; long defined, int * used).
// RUN: rm -rf %t && mkdir -p %t
// RUN: %clang -fsafe-c -c %S/Inputs/safe-c-xtu-def.c -o %t/def.o
// RUN: %clang -fsafe-c -c %s -DGOOD -o %t/good.o
// RUN: %clang %t/good.o %t/def.o -o %t/good
// RUN: %clang -fsafe-c -c %s -o %t/bad.o
// RUN: not %clang %t/bad.o %t/def.o -o %t/bad 2>&1 | FileCheck %s
// Plain C links the mismatch, as C allows.
// RUN: %clang -c %S/Inputs/safe-c-xtu-def.c -o %t/pdef.o
// RUN: %clang -c %s -o %t/pbad.o
// RUN: %clang %t/pbad.o %t/pdef.o -o %t/pbad
// CHECK-DAG: __safec_type_differs_or_defined_without_safe_c.{{_?}}tbl.
// CHECK-DAG: __safec_type_differs_or_defined_without_safe_c.{{_?}}shared.

#ifdef GOOD
extern int tbl[1];
extern long shared;
int add(int a, int b);
int main(void) { return tbl[0] + (int)shared + add(1, 2); }
#else
extern int tbl[1000];
extern int *_Nullable shared;
int main(void) { tbl[900] = 1; return shared ? *shared : 0; }
#endif
