// RUN: %clang -### -c -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s
// RUN: %clang -### -c -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fc-contracts -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fno-c-contracts -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s

// ON: "-fc-contracts"
// OFF-NOT: "-fc-contracts"

// The extension is C-only, and a C++ input is a hard error rather than a
// silently ignored flag: see Frontend/c-contracts-cxx-rejected.cpp.
// RUN: not %clang -c -fc-contracts -x c++ %s -o /dev/null 2>&1 \
// RUN:   | FileCheck -check-prefix=CXX %s
// CXX: error: invalid argument '-fc-contracts' not allowed with 'C++'
