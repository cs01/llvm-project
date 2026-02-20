// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node* _Nullable next;
};

Node* _Nullable getNode();

#pragma clang assume_nonnull begin

void test_while_basic(Node* _Nullable p) {
    while (p) {
        p->value = 1; // OK - p narrowed by while condition
    }
}

void test_while_linked_list(Node* _Nullable head) {
    Node* _Nullable p = head;
    while (p) {
        p->value = 0;
        p = p->next; // OK - p narrowed, so p->next is safe
    }
}

void test_while_nested(Node* _Nullable p) {
    while (p) {
        Node* _Nullable q = p->next;
        while (q) {
            q->value = p->value; // OK - both narrowed
            q = q->next;
        }
        p = p->next;
    }
}

#pragma clang assume_nonnull end
