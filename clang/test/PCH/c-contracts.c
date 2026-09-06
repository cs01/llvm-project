// Test that contract clauses survive a PCH round trip.
// RUN: %clang_cc1 -fc-contracts -emit-pch -o %t %S/c-contracts.h
// RUN: %clang_cc1 -fc-contracts -include-pch %t -ast-dump-all %s | FileCheck %s

int caller(int *q) { return from_pch(q, 1); }
int *caller2(void) { return post_from_pch(1); }
unsigned long caller3(void) { return old_from_pch(1); }

// CHECK:      FunctionDecl {{.*}} from_pch 'int (int *, int)'
// CHECK-NEXT:   ParmVarDecl {{.*}} p 'int *'
// CHECK-NEXT:   ParmVarDecl {{.*}} n 'int'
// CHECK-NEXT:   requires
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '!='
// CHECK:        requires
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '>'

// The result binding survives too, or a 'ensures' would come back from a PCH with
// a predicate referring to a decl that no longer exists.
// CHECK:      FunctionDecl {{.*}} post_from_pch 'int *(int)'
// CHECK:        requires
// CHECK:        ensures
// CHECK-NEXT:     VarDecl {{.*}} r 'int *'
// CHECK-NEXT:     BinaryOperator {{.*}} 'int' '!='

// ContractOldExpr round-trips as its own node.
// CHECK:      FunctionDecl {{.*}} old_from_pch 'unsigned long (unsigned long)'
// CHECK:        ensures
// CHECK:          ContractOldExpr
