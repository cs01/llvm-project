// -fsafe-c expands to bounds safety, nullability safety with a nonnull
// default, and nullability errors. Flags that weaken it are rejected: the only
// escape from Safe C is an __unsafe region in the source.

// RUN: %clang -### -fsafe-c -c %s 2>&1 | FileCheck %s --check-prefix=ON
// ON: "-fbounds-safety"
// ON: "-fsafe-c" "-include" "safec.h" "-fwrapv" "-fsanitize=integer-divide-by-zero,signed-integer-overflow,shift-exponent" "-fsanitize-trap=integer-divide-by-zero,signed-integer-overflow,shift-exponent"
// ON: "-fnullability-safety"
// ON: "-fnullability-default=nonnull"
// ON: "-Werror=nullability-safety"

// RUN: %clang -### -c %s 2>&1 | FileCheck %s --check-prefix=OFF
// OFF-NOT: "-fsafe-c"
// OFF-NOT: "-fbounds-safety"
// OFF-NOT: "-fnullability-safety"

// RUN: not %clang -### -fsafe-c -fno-bounds-safety -fno-nullability-safety \
// RUN:   -fnullability-default=nullable -c %s 2>&1 | FileCheck %s --check-prefix=WEAKEN
// WEAKEN-DAG: error: invalid argument '-fno-bounds-safety' not allowed with '-fsafe-c'
// WEAKEN-DAG: error: invalid argument '-fno-nullability-safety' not allowed with '-fsafe-c'
// WEAKEN-DAG: error: invalid argument '-fnullability-default=nullable' not allowed with '-fsafe-c'

// Restating what -fsafe-c already implies is fine.
// RUN: %clang -### -fsafe-c -fbounds-safety -fnullability-default=nonnull -c %s 2>&1 \
// RUN:   | FileCheck %s --check-prefix=ON

// RUN: %clang -### -fsafe-c -fno-safe-c -c %s 2>&1 | FileCheck %s --check-prefix=OFF
