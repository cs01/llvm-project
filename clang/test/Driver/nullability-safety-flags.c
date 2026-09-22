// RUN: %clang -### -fnullability-safety %s 2>&1 | FileCheck -check-prefix=FLOW %s
// RUN: %clang -### -fnullability-default=nullable %s 2>&1 | FileCheck -check-prefix=DEFAULT %s
// RUN: %clang -### -fnullability-safety -fnullability-default=nullable %s 2>&1 | FileCheck -check-prefix=BOTH %s
// RUN: %clang -### -fnullability-safety -fno-nullability-stdlib-annotations %s 2>&1 | FileCheck -check-prefix=NOSTDLIB %s
// The stdlib annotation list is on by default, so cc1 should not get any flag for it.
// RUN: %clang -### -fnullability-safety %s 2>&1 | FileCheck -check-prefix=STDLIB-DEFAULT %s

// FLOW: "-fnullability-safety"
// DEFAULT: "-fnullability-default=nullable"
// BOTH: "-fnullability-safety"
// BOTH: "-fnullability-default=nullable"
// NOSTDLIB: "-fno-nullability-stdlib-annotations"
// STDLIB-DEFAULT-NOT: "-fno-nullability-stdlib-annotations"
