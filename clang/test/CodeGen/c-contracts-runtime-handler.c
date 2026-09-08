// RUN: %clang_cc1 -fc-contracts -fcontract-runtime-checks -emit-llvm -o - %s | FileCheck %s
// RUN: %clang -O2 -fc-contracts -fcontract-runtime-checks \
// RUN:   -Wno-contract-violation %s -o %t && %t

#include <c_contracts.h>

int checked(int n) pre (n > 0) { return n; }

static int violations;

void __contract_violation(const char *predicate, const char *file,
                          unsigned line, const char *function) {
  ++violations;
}

int main(void) {
  int result = checked(0);
  return result != 0 || violations != 1;
}

// CHECK: call void @__contract_violation(
// CHECK-NEXT: br label %contract.ok
// CHECK-LABEL: define{{.*}} void @__contract_violation(
// CHECK-NOT: define weak void @__contract_violation(
