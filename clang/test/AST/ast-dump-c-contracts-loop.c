// RUN: %clang_cc1 -fc-contracts -ast-dump %s | FileCheck %s

void fill(int *buf, unsigned len) {
  unsigned i = 0;
  while (i < len)
    loop_invariant (i <= len)
    decreases      (len - i)
  {
    buf[i] = 0;
    i++;
  }
}
// CHECK:      WhileStmt
// CHECK-NEXT:   loop_invariant
// CHECK-NEXT:     BinaryOperator {{.*}} '<='
// CHECK:        decreases
// CHECK-NEXT:     BinaryOperator {{.*}} '-'

// All three loop forms carry clauses.
void all_forms(int *buf, unsigned len) {
  for (unsigned i = 0; i < len; i++)
    loop_invariant (i <= len)
  { buf[i] = 0; }

  unsigned j = 0;
  do
    decreases      (len - j)
  { j++; } while (j < len);
}
// CHECK:      ForStmt
// CHECK-NEXT:   loop_invariant
// CHECK:      DoStmt
// CHECK-NEXT:   decreases
