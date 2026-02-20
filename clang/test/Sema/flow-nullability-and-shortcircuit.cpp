// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node* _Nullable next;
};

#pragma clang assume_nonnull begin

void test_and_basic(Node* _Nullable p) {
    if (p && p->value == 42) {
        p->value = 0; // OK - p narrowed by && LHS
    }
}

void test_and_star(Node* _Nullable p) {
    if (p && (*p).value == 42) {
        (*p).value = 0; // OK
    }
}

// CFG decomposes chained && into separate blocks, so member narrowing
// works correctly at each stage.
void test_and_chained_no_warning(Node* _Nullable p) {
    if (p && p->next && p->next->value > 0) {
        p->next->value = 0; // OK - member narrowing works throughout
    }
}

void test_and_two_vars(Node* _Nullable p, Node* _Nullable q) {
    if (p && q) {
        p->value = q->value; // OK - both narrowed
    }
}

void test_and_three_vars(Node* _Nullable a, Node* _Nullable b, Node* _Nullable c) {
    if (a && b && c) {
        a->value = b->value + c->value; // OK
    }
}

void test_and_member_two_part(Node* _Nullable p) {
    if (p && p->next) {
        p->next->value = 1; // OK - both p and p->next narrowed
    }
}

#pragma clang assume_nonnull end
