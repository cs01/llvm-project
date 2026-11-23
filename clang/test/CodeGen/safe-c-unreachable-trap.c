// Under -fsafe-c, __builtin_unreachable() traps when reached and
// __builtin_assume(c) traps when c is false (Rust's unreachable! and assert!),
// instead of being undefined behavior the optimizer may exploit.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c %s \
// RUN:   | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=PLAIN

int unreach(int i) {
  if (i > 3)
    __builtin_unreachable();
  return i;
}
// SAFEC-LABEL: define {{.*}} @unreach(
// SAFEC: call void @llvm.ubsantrap(
// PLAIN-LABEL: define {{.*}} @unreach(
// PLAIN-NOT: ubsantrap
// PLAIN: unreachable

int assume(int i) {
  __builtin_assume(i < 8);
  return i;
}
// SAFEC-LABEL: define {{.*}} @assume(
// SAFEC-NOT: @llvm.assume
// SAFEC: call void @llvm.ubsantrap(
// PLAIN-LABEL: define {{.*}} @assume(
// PLAIN: call void @llvm.assume(
