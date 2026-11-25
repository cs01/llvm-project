// -fsafe-c code lowers to CIR for analysis: plain pointers, no bounds checks,
// each operand evaluated once. It cannot be compiled that way.
// RUN: %clang -fsafe-c -fclangir -emit-cir %s -o - | FileCheck %s
// RUN: not %clang -fsafe-c -fclangir -c %s -o /dev/null 2>&1 | FileCheck %s --check-prefix=NOCODE
// NOCODE: it can only emit CIR for analysis; remove -fclangir to compile
// NOCODE-NOT: error:

#include <ptrcheck.h>
#include <stddef.h>

struct buf { int *__counted_by(n) p; size_t n; };
int *__counted_by(n) make(size_t n);

// The shared operand 'make(n)' appears once.
// CHECK-LABEL: cir.func{{.*}} @fill
// CHECK-COUNT-1: cir.call @make
// CHECK-NOT: cir.call @make
// CHECK: cir.return
void fill(struct buf *b, size_t n) {
  b->p = make(n);
  b->n = n;
}

// A group whose first assignment sits in a block of its own.
// CHECK-LABEL: cir.func{{.*}} @split
// CHECK: cir.get_member {{.*}} {name = "p"}
// CHECK: cir.get_member {{.*}} {name = "n"}
void split(struct buf *b, int *__counted_by(n) q, size_t n) {
  __unsafe { b->p = q; }
  b->n = n;
}

// CHECK-LABEL: cir.func{{.*}} @index
// CHECK: cir.ptr_stride
// CHECK-NOT: cir.trap
// CHECK: cir.return
int index(int *__counted_by(n) p, size_t n, size_t i) {
  int local[4] = {1, 2, 3, 4};
  return p[i] + local[i];
}

// A move: the place is read and then null.
// CHECK-LABEL: cir.func{{.*}} @take
// CHECK: %[[F:.*]] = cir.get_member {{.*}} {name = "p"}
// CHECK: cir.load {{.*}}%[[F]]
// CHECK: cir.store {{.*}}%[[F]]
struct one { int *__single _Nullable p; };
int *__single _Nullable take(struct one *o) {
  return __builtin_safec_take(o->p);
}
