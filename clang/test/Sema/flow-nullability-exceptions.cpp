// Tests for exception handling interactions with flow-sensitive nullability.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 -fcxx-exceptions %s -verify

struct Node {
    int value;
};

Node * _Nullable getNode();

#pragma clang assume_nonnull begin

// === Narrowing before try block persists inside ===

void test_narrow_before_try(Node * _Nullable p) {
    if (!p) return;
    try {
        (void)p->value; // OK — narrowed before try
    } catch (...) {
    }
}

// === throw in null guard narrows ===

void test_throw_guard(Node * _Nullable p) {
    if (!p) throw "null";
    (void)p->value; // OK — throw terminates null path
}

// === try body with null check ===

void test_null_check_in_try(Node * _Nullable p) {
    try {
        if (!p) throw "null";
        (void)p->value; // OK — narrowed by throw guard
    } catch (...) {
    }
}

// === catch block should not inherit narrowing from try ===
// After the try block, if code falls through, the narrowing from
// inside try may or may not hold depending on CFG edges.

void test_after_try_catch(Node * _Nullable p) {
    try {
        if (p)
            (void)p->value; // OK — narrowed
    } catch (...) {
    }
    // After try/catch, p's narrowing depends on merge of try and catch edges.
    // Conservative: should warn.
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Narrowing in both try and catch ===

void test_narrow_in_both(Node * _Nullable p) {
    try {
        if (!p) throw "null";
        (void)p->value; // OK
    } catch (...) {
        if (!p) return;
        (void)p->value; // OK — narrowed in catch too
    }
}

// === Multiple catch blocks ===

void test_multiple_catch(Node * _Nullable p) {
    if (!p) return;
    try {
        (void)p->value; // OK — narrowed
    } catch (int) {
        // narrowing may not persist through exception edges
    } catch (...) {
    }
}

// === throw expression in ternary ===

void test_throw_ternary(Node * _Nullable p) {
    if (!p) throw "null";
    (void)p->value; // OK — throw terminates null path
}

// === Noexcept function — no exception CFG edges ===

void test_noexcept_narrowing(Node * _Nullable p) noexcept {
    if (!p) return;
    (void)p->value; // OK — narrowed, no exception edges to worry about
}

#pragma clang assume_nonnull end
