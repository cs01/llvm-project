// RUN: not %clang_cc1 -fsyntax-only -fc-contracts -fcontract-emit-cprover-unit %s 2>&1 \
// RUN:   | FileCheck %s

// A clause that does not type-check has no rewrite, so its original text would
// survive into the output beside the clauses that were rewritten -- a file that
// is half this grammar and half CBMC's, compiling as neither, with nothing at
// the top saying which half is missing. Refuse the whole unit instead.

int impure(void);

int good(int n) pre (n > 0);
int bad(int n) pre (impure() > 0);

// CHECK: error: contract predicate must be free of side effects
// CHECK: error: refusing to rewrite this translation unit: 1 contract clause did not type-check

// And nothing of the rewritten unit is printed.
// CHECK-NOT: __CPROVER_requires
