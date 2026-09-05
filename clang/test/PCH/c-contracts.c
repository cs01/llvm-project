// Test that contract clauses survive a PCH round trip.
// RUN: %clang_cc1 -fc-contracts -emit-pch -o %t %S/c-contracts.h
// RUN: %clang_cc1 -fc-contracts -include-pch %t -ast-dump-all %s | FileCheck %s

int caller(int *q) { return from_pch(q, 1); }

// CHECK:      FunctionDecl {{.*}} from_pch 'int (int *, int)'
// CHECK-NEXT:   ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT:   ParmVarDecl {{.*}} n 'int'
// CHECK-NEXT:   pre
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '!='
// CHECK:        pre
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '>'
