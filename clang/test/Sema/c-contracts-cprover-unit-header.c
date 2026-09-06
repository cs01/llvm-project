// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit -verify %s
// RUN: %clang_cc1 -E %s -o %t.i
// RUN: %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %t.i 2>&1 | FileCheck %s

// The rewriter can only replace spans in the main file, so a contract declared
// in an included header has nothing to rewrite. Staying silent would hand back
// a translation unit that proves less than the author wrote, so it warns.
#include "Inputs/c-contracts-api.h"
// expected-warning@1 {{2 contract clauses declared outside this file were not rewritten; preprocess first so header contracts are part of the translation unit}}

int *use(void) { return allocate(8); }

// Preprocessing first makes the header part of the translation unit, which is
// the documented workflow anyway since CBMC's own front end wants preprocessed
// source. The clauses are then rewritten, and there is nothing left to warn
// about.
//
// CHECK-NOT: warning
// CHECK:      int *allocate(unsigned long n)
// CHECK-NEXT:   __CPROVER_requires(n > 0)
// CHECK-NEXT:   __CPROVER_ensures(__CPROVER_return_value != 0);
// CHECK-NOT: warning
