// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node* _Nullable next;
};

Node* _Nullable getNode();

#pragma clang assume_nonnull begin

// === Basic ternary narrowing ===

void test_ternary_true_branch(Node* _Nullable p) {
    int x = p ? p->value : 0; // OK - p narrowed to nonnull in true branch
}

void test_ternary_false_branch_negated(Node* _Nullable p) {
    int x = !p ? 0 : p->value; // OK - p narrowed to nonnull in false branch
}

void test_ternary_no_narrowing_false(Node* _Nullable p) {
    int x = p ? 0 : p->value; // expected-warning{{dereferencing nullable pointer of type 'Node * _Nullable'}}
}

void test_ternary_deref_star(Node* _Nullable p) {
    Node n = p ? *p : (Node){0, nullptr}; // OK - p narrowed to nonnull
    (void)n;
}

// === Ternary with comparison operators ===

void test_ternary_ne_null(Node* _Nullable p) {
    int x = (p != nullptr) ? p->value : -1; // OK
}

void test_ternary_eq_null(Node* _Nullable p) {
    int x = (p == nullptr) ? -1 : p->value; // OK - narrowed in false branch
}

// === Ternary with AND conditions ===

void test_ternary_and_both(Node* _Nullable p, Node* _Nullable q) {
    int x = (p && q) ? p->value + q->value : 0; // OK - both narrowed
}

// === Nested ternary ===

void test_nested_ternary(Node* _Nullable p, Node* _Nullable q) {
    int x = p ? (q ? p->value + q->value : p->value) : 0; // OK
}

// === Ternary warns when it should ===

void test_ternary_unrelated_cond(int flag, Node* _Nullable p) {
    int x = flag ? p->value : 0; // expected-warning{{dereferencing nullable pointer of type 'Node * _Nullable'}}
}

#pragma clang assume_nonnull end
