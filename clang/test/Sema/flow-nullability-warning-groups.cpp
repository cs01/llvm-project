// Test warning group suppression flags.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-flow-nullable-dereference %s -verify
// expected-no-diagnostics

void test_suppressed(int * _Nullable p) {
    *p = 42; // Would warn without -Wno-flow-nullable-dereference
}
