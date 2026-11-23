// -fsafe-c expands to bounds safety, nullability safety with a nonnull
// default, and nullability errors; each piece can still be overridden.

// RUN: %clang -### -fsafe-c -c %s 2>&1 | FileCheck %s --check-prefix=ON
// ON: "-fbounds-safety"
// ON: "-fsafe-c"
// ON: "-fnullability-safety"
// ON: "-fnullability-default=nonnull"
// ON: "-Werror=nullability-safety"

// RUN: %clang -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// OFF-NOT: "-fsafe-c"
// OFF-NOT: "-fbounds-safety"
// OFF-NOT: "-fnullability-safety"

// RUN: %clang -### -fsafe-c -fno-bounds-safety -fnullability-default=nullable -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=OVERRIDE
// OVERRIDE-NOT: "-fbounds-safety"
// OVERRIDE: "-fnullability-safety"
// OVERRIDE: "-fnullability-default=nullable"
// OVERRIDE-NOT: "-fnullability-default=nonnull"

// RUN: %clang -### -fsafe-c -fno-safe-c -c %s 2>&1 | FileCheck %s --check-prefix=OFF
