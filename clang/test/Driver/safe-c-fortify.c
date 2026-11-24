// -fsafe-c turns libc's _FORTIFY_SOURCE off: its __builtin___*_chk calls
// would skip the bounds-annotated prototypes Safe C checks memcpy through.
// RUN: %clang -### -fsafe-c -c %s 2>&1 | FileCheck %s --check-prefix=SAFEC
// RUN: %clang -### -c %s 2>&1 | FileCheck %s --check-prefix=PLAIN
// SAFEC: "-U_FORTIFY_SOURCE" "-D_FORTIFY_SOURCE=0"
// PLAIN-NOT: _FORTIFY_SOURCE
