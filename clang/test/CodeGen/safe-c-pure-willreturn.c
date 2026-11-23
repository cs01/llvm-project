// Safe C checks pure/const bodies for memory effects, not termination, so it
// keeps memory(read)/memory(none) but drops willreturn: a call that may loop
// forever must not be deleted.
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - -fsafe-c %s \
// RUN:   | FileCheck %s --check-prefix=SAFEC
// RUN: %clang_cc1 -triple x86_64-unknown-linux-gnu -emit-llvm -o - %s \
// RUN:   | FileCheck %s --check-prefix=PLAIN

__attribute__((const)) int spin(int x) {
  while (x) {
  }
  return 0;
}

int caller(int x) { return spin(x); }

// SAFEC: call i32 @spin(i32 {{.*}}) #[[A:[0-9]+]]
// SAFEC: attributes #[[A]] = {
// SAFEC-NOT: willreturn
// SAFEC-SAME: memory(none)
// SAFEC-NOT: willreturn
// SAFEC-SAME: }
// PLAIN: call i32 @spin(i32 {{.*}}) #[[A:[0-9]+]]
// PLAIN: attributes #[[A]] = { {{.*}}willreturn
