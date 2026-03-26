// Tests for __attribute__((nonnull)) interactions with flow-sensitive nullability.
// The analysis already handles NonNullAttr for parameter narrowing — this
// provides comprehensive test coverage.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
};

#pragma clang assume_nonnull begin

// === Function-level __attribute__((nonnull)) ===

__attribute__((nonnull))
void consume_all(Node *a, Node *b) {
    // params are nonnull by attribute
}

void test_fn_level_nonnull(Node * _Nullable p, Node * _Nullable q) {
    if (!p || !q) return;
    consume_all(p, q); // OK — both narrowed
    // After passing to nonnull function, narrowing is preserved
    (void)p->value; // OK
    (void)q->value; // OK
}

// === Parameter-level __attribute__((nonnull(1,3))) ===

__attribute__((nonnull(1, 3)))
void consume_specific(Node *a, Node * _Nullable b, Node *c) {
    // a and c are nonnull by attribute
}

void test_param_level_nonnull(Node * _Nullable p, Node * _Nullable q, Node * _Nullable r) {
    consume_specific(p, q, r);
    // After call: p and r were passed to nonnull params, so they're narrowed
    (void)p->value; // OK — narrowed by passing to nonnull param 1
    (void)r->value; // OK — narrowed by passing to nonnull param 3
}

// === __attribute__((returns_nonnull)) ===

__attribute__((returns_nonnull))
Node *createSafe();

void test_returns_nonnull() {
    Node *p = createSafe();
    (void)p->value; // OK — _Nonnull return type
}

// === Narrowing via _Nonnull parameter (type qualifier, not attribute) ===

void take_nonnull(Node * _Nonnull p) {}

void test_type_qualifier_narrowing(Node * _Nullable p) {
    take_nonnull(p);
    (void)p->value; // OK — narrowed by passing to _Nonnull param
}

// === Multiple calls to nonnull functions ===

void test_multi_call_narrowing(Node * _Nullable a, Node * _Nullable b) {
    take_nonnull(a);
    take_nonnull(b);
    (void)a->value; // OK
    (void)b->value; // OK
}

// === Nonnull parameter not invalidated by other calls ===

void unrelated_fn();

void test_nonnull_survives_calls(Node * _Nonnull p) {
    unrelated_fn();
    (void)p->value; // OK — _Nonnull parameter, calls don't invalidate
}

// === Attribute on C-style function declaration ===

extern "C" {
    __attribute__((nonnull(1)))
    void c_consumer(Node *p, int x);
}

void test_c_fn_nonnull(Node * _Nullable p) {
    c_consumer(p, 42);
    (void)p->value; // OK — narrowed by passing to nonnull param
}

#pragma clang assume_nonnull end
