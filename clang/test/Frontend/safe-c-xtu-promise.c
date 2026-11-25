// Two files that disagree on annotations, or on a struct they share, do not
// link (blind attack cause 32, 2026-11-24).
// RUN: rm -rf %t && mkdir %t
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -c %S/Inputs/safe-c-xtu-promise-def.c -o %t/def.o
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -c %s -DGOOD -o %t/good.o && %clang %t/good.o %t/def.o -o %t/good
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -c %s -o %t/bad.o
// RUN: not %clang %t/bad.o %t/def.o -o %t/bad 2>&1 | FileCheck %s
// CHECK-DAG: type_differs_or_defined_without_safe_c.{{_?}}frees.
// CHECK-DAG: type_differs_or_defined_without_safe_c.{{_?}}counts.
// CHECK-DAG: type_differs_or_defined_without_safe_c.{{_?}}look.struct.shared.
#include <stdlib.h>
#include <safec.h>
struct opaque;
#ifdef GOOD
struct shared { int a; long b; };
int agree(struct opaque *o, int *__counted_by(n) p, int n, struct shared *s);
int run(struct opaque *o, struct shared *s) { int a[2] = {1, 2}; return agree(o, a, 2, s); }
int main(void) { return 0; }
#else
struct shared { int a; int *_Static _Nullable b; };
void frees(int *p);
void counts(int *p, int n);
int look(struct shared *s);
int main(void) {
  int *_Nullable p = malloc(4);
  struct shared s = {0};
  if (!p) return 1;
  frees(p);
  counts(p, 1);
  int r = look(&s) + *p;
  free(p);
  return r;
}
#endif
