// RUN: %clang_cc1 -fc-contracts -ast-dump %s | FileCheck %s

int f(int *p, int n) requires (p != 0) requires (n > 0);
// CHECK:      FunctionDecl {{.*}} f 'int (int *, int)'
// CHECK-NEXT:   ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT:   ParmVarDecl {{.*}} n 'int'
// CHECK-NEXT:   requires
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '!='
// CHECK:        requires
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '>'
