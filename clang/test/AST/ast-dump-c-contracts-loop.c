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

// All three loop forms carry clauses.
void all_forms(int *buf, unsigned len) {
  for (unsigned i = 0; i < len; i++)
    invariant (i <= len)
  { buf[i] = 0; }

  unsigned j = 0;
  do
    variant (len - j)
  { j++; } while (j < len);
}
// CHECK:      ForStmt
// CHECK-NEXT:   invariant
// CHECK:      DoStmt
// CHECK-NEXT:   variant
