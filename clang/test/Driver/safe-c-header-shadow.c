// -fsafe-c force-includes the compiler's own safec.h by full path, so a
// project file that happens to be named safec.h cannot replace it.
// RUN: rm -rf %t && mkdir -p %t && echo '#error project safec.h used' > %t/safec.h
// RUN: cd %t && %clang -fsafe-c -fsyntax-only %s
// RUN: cd %t && %clang -fsafe-c -### -c %s 2>&1 | FileCheck %s
// CHECK: "-include" "{{.+}}safec.h"
int main(void) { return 0; }
