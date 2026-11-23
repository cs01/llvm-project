// -fsafe-c expands to bounds safety, nullability safety with a nonnull
// default, and nullability errors. Flags that weaken it are rejected: the only
// escape from Safe C is an __unsafe region in the source.

// RUN: %clang -### -fsafe-c -c %s 2>&1 | FileCheck %s --check-prefix=ON
// ON: "-fbounds-safety"
// ON: "-fsafe-c" "-include" "safec.h" "-ftrivial-auto-var-init=zero" "-fwrapv"
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



// Flags found by the 2026-11-23 review that turn checks off are rejected too,
// including through -Xclang, and -fno-safe-c cannot follow -fsafe-c.
// RUN: not %clang -### -fsafe-c -ftrap-function=f -fbounds-safety-soft-traps=call-minimal \
// RUN:   -fno-nullability-libc-nullable-returns -ftrivial-auto-var-init=pattern \
// RUN:   -Xclang -ftrivial-auto-var-init=uninitialized -c %s 2>&1 | FileCheck %s --check-prefix=MORE
// MORE-DAG: error: invalid argument '-ftrap-function=f' not allowed with '-fsafe-c'
// MORE-DAG: error: invalid argument '-fbounds-safety-soft-traps=call-minimal' not allowed with '-fsafe-c'
// MORE-DAG: error: invalid argument '-fno-nullability-libc-nullable-returns' not allowed with '-fsafe-c'
// MORE-DAG: error: invalid argument '-ftrivial-auto-var-init=pattern' not allowed with '-fsafe-c'
// MORE-DAG: error: invalid argument '-Xclang -ftrivial-auto-var-init=uninitialized' not allowed with '-fsafe-c'
// RUN: not %clang -### -fsafe-c -fno-safe-c -c %s 2>&1 | FileCheck %s --check-prefix=NOSAFE
// NOSAFE: error: invalid argument '-fno-safe-c' not allowed with '-fsafe-c'

// null and vla-bound traps, and const string literals.
// RUN: %clang -### -fsafe-c -c %s 2>&1 | FileCheck %s --check-prefix=TRAPS
// TRAPS: "-fsanitize=integer-divide-by-zero,signed-integer-overflow,shift-exponent,null,vla-bound"
// TRAPS: "-fconst-strings"
