// RUN: not %clang_cc1 -triple riscv32-unknown-elf -fclangir -emit-llvm \
// RUN:     %s -o %t.ll 2>&1 | FileCheck %s

// LoweringPrepare emits an MLIR-side error via mlir::Operation::emitError for
// a thread-safe function-local static on a target without inline atomics
// (riscv32 without the A extension). CIRDiagnosticHandler must surface it in
// clang's `file:line:col: error: ...` format rather than MLIR's
// `loc("file":N:M): error: ...` default.

// CHECK: mlir-error-routing.cpp:[[#@LINE+7]]:12: error: NYI: guarded init without inline atomics support
// CHECK-NOT: loc({{.*}}): error: NYI: guarded init

struct S {
  S();
};
S &get() {
  static S s;
  return s;
}

// The generic CIR-to-CIR transform fatal error must not be reported on top of
// the specific one: CIRGenAction gates it on hasErrorOccurred().
// CHECK-NOT: error: CIR-to-CIR transformation failed
