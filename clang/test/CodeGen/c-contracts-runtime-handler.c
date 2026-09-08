// RUN: %clang_cc1 -fc-contracts -fcontract-runtime-checks -emit-llvm -o - %s | FileCheck %s

#include <c_contracts.h>

int checked(int n) pre (n > 0) { return n; }

void custom_handler_ran(void);

void __contract_violation(const char *predicate, const char *file,
                          unsigned line, const char *function) {
  custom_handler_ran();
  __builtin_unreachable();
}

// CHECK: call void @__contract_violation(
// CHECK-LABEL: define{{.*}} void @__contract_violation(
// CHECK: call void @custom_handler_ran()
// CHECK-NOT: define weak void @__contract_violation(
