// RUN: %clang_cc1 -fc-contracts -fcontract-runtime-checks -emit-llvm -o - %s \
// RUN:   | FileCheck %s
// RUN: %clang_cc1 -fc-contracts -emit-llvm -o - %s | FileCheck -check-prefix=OFF %s

typedef unsigned long size_t;

// A checkable clause becomes a branch to a call, not an inline trap: a project
// with its own fault handler has to be able to replace the handler.
int half(int n) pre (n > 0) { return n / 2; }
// CHECK-LABEL: define {{.*}}@half
// CHECK: br i1 {{.*}}, label %contract.ok, label %contract.broken
// CHECK: contract.broken:
// CHECK: call void @__contract_violation(

// The handler is weak, so nothing has to be linked in and any strong
// definition in the program wins.
// CHECK: define weak void @__contract_violation(
// CHECK: call void @llvm.trap()

// Without the flag there is no check at all: contracts stay declaration-level.
// OFF-NOT: __contract_violation
// OFF-NOT: contract.broken

// A clause about an allocation cannot be checked at entry, and is declined
// rather than silently passed.
void takes(const void *p, size_t n) pre (readable(p, n)) {}
// expected-warning@-1 {{cannot be checked at run time}}
// CHECK-LABEL: define {{.*}}@takes
// CHECK-NOT: contract.broken
