// RUN: %clang -### -c -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s
// RUN: %clang -### -c -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fc-contracts -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fno-c-contracts -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s

// Every contract flag the reference documents as a user flag has to survive the
// driver, not just -cc1: the unit rewriter did not, and the proof script only
// worked because it called -cc1 itself.
// RUN: %clang -### -c -fc-contracts -fcontract-emit-cprover %s 2>&1 \
// RUN:   | FileCheck -check-prefix=EMIT %s
// RUN: %clang -### -c -fc-contracts -fcontract-emit-cprover-unit %s 2>&1 \
// RUN:   | FileCheck -check-prefix=UNIT %s
// EMIT: "-fcontract-emit-cprover"
// UNIT: "-fcontract-emit-cprover-unit"

// ON: "-fc-contracts"
// OFF-NOT: "-fc-contracts"

// The extension is C-only, and a C++ input is a hard error rather than a
// silently ignored flag: see Frontend/c-contracts-cxx-rejected.cpp.
// RUN: not %clang -c -fc-contracts -x c++ %s -o /dev/null 2>&1 \
// RUN:   | FileCheck -check-prefix=CXX %s
// CXX: error: invalid argument '-fc-contracts' not allowed with 'C++'

// RUN: not %clang -fsyntax-only -fcontract-runtime-checks %s 2>&1 \
// RUN:   | FileCheck -check-prefix=NEEDS-FLAG %s
// RUN: not %clang -fsyntax-only -fc-contracts -fcontract-emit-harness %s 2>&1 \
// RUN:   | FileCheck -check-prefix=NEEDS-UNIT %s
// NEEDS-FLAG: error: invalid argument '-fcontract-runtime-checks' only allowed with '-fc-contracts'
// NEEDS-UNIT: error: invalid argument '-fcontract-emit-harness' only allowed with '-fcontract-emit-cprover-unit'
