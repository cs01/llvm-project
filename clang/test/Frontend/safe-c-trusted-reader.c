// A trusted (system) header's unannotated `const char *` parameter with no
// length can only be a terminated string, read to its end: a buffer passed
// to it is checked for a terminator within its bounds at the call (as for
// libc readers). With a length parameter it is not assumed to be a string.
// RUN: %clang_cc1 -triple arm64-apple-macosx -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -isystem %S/Inputs -emit-llvm -o - %s | FileCheck %s
#include <safe-c-trusted-reader.h>
void f(void) {
  char buf[4] = {'a', 'b', 'c', 'd'};
  print_line(buf);
  print_n(buf, 4);
}
// CHECK-LABEL: define {{.*}} @f(
// CHECK: terminated_by.loop
// CHECK: call void @print_line(
// CHECK-NOT: terminated_by.loop
// CHECK: call void @print_n(
