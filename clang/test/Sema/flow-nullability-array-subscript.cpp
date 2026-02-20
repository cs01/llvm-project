// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// Array subscript (p[0]) is semantically a dereference (*(p + 0)).
// The CFG-based analysis checks array subscript base pointers.

#pragma clang assume_nonnull begin

void test_subscript_warns(int* _Nullable p) {
    p[0] = 42; // expected-warning{{dereferencing nullable pointer of type 'int * _Nullable'}}
}

void test_subscript_after_check(int* _Nullable p) {
    if (p) {
        p[0] = 42; // OK - narrowed by check
    }
}

void test_subscript_offset(int* _Nullable p) {
    p[5] = 42; // expected-warning{{dereferencing nullable pointer of type 'int * _Nullable'}}
}

#pragma clang assume_nonnull end
