// Safe C turns on the Darwin libc bounds annotations, as -fbounds-safety does:
// without them strcpy into a counted buffer compiled and overflowed.
// RUN: %clang -### -target arm64-apple-macos14 -fsafe-c -c %s 2>&1 | FileCheck %s
// RUN: %clang -### -target arm64-apple-macos14 -c %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// CHECK: "-D__LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES"
// PLAIN-NOT: __LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES
