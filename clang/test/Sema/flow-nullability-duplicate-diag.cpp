// Verify no confusing duplicate diagnostics when both
// -Wnullable-to-nonnull-conversion and -Wflow-nullable-dereference are active.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wnullable-to-nonnull-conversion -std=c++17 %s -verify

#pragma clang assume_nonnull begin

void take_nonnull(int * _Nonnull p);

// Passing nullable to nonnull param: only the conversion warning fires.
// The flow analysis then narrows the pointer, so no deref warning after.
void test_pass_to_nonnull(int * _Nullable p) {
    take_nonnull(p); // expected-warning{{implicit conversion from nullable pointer 'int * _Nullable' to non-nullable pointer type 'int * _Nonnull'}}
    *p = 42; // OK — narrowed by nonnull call, no second warning
}

// Direct dereference of nullable: only the flow warning fires,
// not the conversion warning (there's no nonnull destination type).
void test_deref_only(int * _Nullable p) {
    *p = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Assignment to nonnull variable from nullable: only the conversion warning.
void test_assign_to_nonnull(int * _Nullable p) {
    int * _Nonnull q = p; // expected-warning{{implicit conversion from nullable}}
}

// After null check: flow analysis knows p is non-null, so no deref warning.
// But -Wnullable-to-nonnull-conversion is type-based, not flow-based — the
// declared type is still _Nullable, so the conversion warning persists.
// This is correct: the two warnings serve different purposes.
void test_checked(int * _Nullable p) {
    if (!p) return;
    take_nonnull(p); // expected-warning{{implicit conversion from nullable}}
    *p = 42;         // OK — narrowed, no deref warning
}

#pragma clang assume_nonnull end
