// Under -fsafe-c, malloc(n) is emitted as calloc(1, n): an uninitialized
// pointer in heap memory would be a wild pointer, a zeroed one is null and
// traps on use. -fno-builtin does not turn this off.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c %s \
// RUN:   | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c \
// RUN:   -fno-builtin %s | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=PLAIN

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t);

struct node { int v; long next; };

__attribute__((ownership_returns(malloc))) struct node *make(void) { return malloc(sizeof(struct node)); }

// SAFEC-LABEL: define {{.*}} @make(
// SAFEC: call noalias ptr @calloc(i64 1, i64 16)
// SAFEC-NOT: @malloc
// PLAIN-LABEL: define {{.*}} @make(
// PLAIN: call {{.*}}ptr @malloc(i64 {{.*}}16)
