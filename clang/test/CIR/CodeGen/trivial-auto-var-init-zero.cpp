// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=zero %s -o - | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -ftrivial-auto-var-init=zero -ftrivial-auto-var-init-max-size=8 %s -o - | FileCheck --check-prefix=MAX %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir %s -o - | FileCheck --check-prefix=OFF %s

struct Padded {
  char c;
  int i;
};

void use(void *);

void scalar() {
  int x;
  use(&x);
}

void record() {
  Padded p;
  use(&p);
}

void array() {
  int a[16];
  use(a);
}

void constAggregate() {
  Padded p = {1, 2};
  use(&p);
}

void optOut() {
  [[clang::uninitialized]] int x;
  use(&x);
}

// CHECK-LABEL: cir.func {{.*}} @_Z6scalarv
// CHECK: cir.libc.memset {{.*}} bytes at {{.*}} to {{.*}} : !cir.ptr<!void>, !u8i, !u64i
// CHECK-LABEL: cir.func {{.*}} @_Z6recordv
// CHECK: cir.libc.memset
// CHECK-LABEL: cir.func {{.*}} @_Z5arrayv
// CHECK: cir.libc.memset
// CHECK-LABEL: cir.func {{.*}} @_Z14constAggregatev
// CHECK: cir.libc.memset
// CHECK: cir.copy
// CHECK-LABEL: cir.func {{.*}} @_Z6optOutv
// CHECK-NOT: cir.libc.memset
// CHECK: cir.return

// MAX-LABEL: cir.func {{.*}} @_Z6recordv
// MAX: cir.libc.memset
// MAX-LABEL: cir.func {{.*}} @_Z5arrayv
// MAX-NOT: cir.libc.memset
// MAX: cir.return

// OFF-NOT: cir.libc.memset
