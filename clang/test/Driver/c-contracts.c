// RUN: %clang -### -c -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s
// RUN: %clang -### -c -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fc-contracts -fno-c-contracts %s 2>&1 | FileCheck -check-prefix=OFF %s
// RUN: %clang -### -c -fno-c-contracts -fc-contracts %s 2>&1 | FileCheck -check-prefix=ON %s

// ON: "-fc-contracts"
// OFF-NOT: "-fc-contracts"
