// RUN: %clang_cc1 -fc-contracts -ast-dump %s | FileCheck %s

int f(int *p, int n) pre (p != 0) pre (n > 0);
// CHECK:      FunctionDecl {{.*}} f 'int (int *, int)'
// CHECK-NEXT:   ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT:   ParmVarDecl {{.*}} n 'int'
// CHECK-NEXT:   pre
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '!='
// CHECK:        pre
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '>'
