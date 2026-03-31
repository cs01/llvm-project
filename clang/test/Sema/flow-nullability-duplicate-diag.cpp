// Verify no confusing duplicate diagnostics when both
// -Wnullable-to-nonnull-conversion and -Wflow-nullable-dereference are active.
// When flow-sensitive nullability is on, the type-based -Wnullable-to-nonnull-conversion
// is fully suppressed — the flow analysis provides strictly better coverage.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wnullable-to-nonnull-conversion -std=c++17 %s -verify

#pragma clang assume_nonnull begin

void take_nonnull(int * _Nonnull p);

// Passing nullable to nonnull param: only the flow-sensitive argument warning fires.
// The flow analysis then narrows the pointer, so no deref warning after.
void test_pass_to_nonnull(int * _Nullable p) {
    take_nonnull(p); // expected-warning{{passing nullable pointer to nonnull parameter}} expected-note{{add a null check before the call}}
    *p = 42; // OK — narrowed by nonnull call, no second warning
}

// Direct dereference of nullable: only the flow warning fires.
void test_deref_only(int * _Nullable p) {
    *p = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Assignment to nonnull variable from nullable: flow-sensitive assignment warning.
void test_assign_to_nonnull(int * _Nullable p) {
    int * _Nonnull q = p; // expected-warning{{assigning nullable pointer to nonnull variable}} expected-note{{add a null check before assigning}}
}

// After null check: flow analysis knows p is non-null, so no warnings at all.
// The type-based warning would have fired here (declared type is still _Nullable),
// but flow-sensitive analysis correctly sees p is narrowed.
void test_checked(int * _Nullable p) {
    if (!p) return;
    take_nonnull(p); // OK — narrowed, no warning
    *p = 42;         // OK — narrowed, no deref warning
}

#pragma clang assume_nonnull end
