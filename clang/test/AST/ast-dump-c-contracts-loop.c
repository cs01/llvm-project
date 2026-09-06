// RUN: %clang_cc1 -fc-contracts -ast-dump %s | FileCheck %s

void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    invariant (i <= len)
    variant   (len - i)
  {
    buf[i] = 0;
    i++;
  }
}
// CHECK:      WhileStmt
// CHECK-NEXT:   invariant
// CHECK-NEXT:     BinaryOperator {{.*}} '<='
// CHECK:        variant
// CHECK-NEXT:     BinaryOperator {{.*}} '-'
