// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node* _Nullable next;
};

#pragma clang assume_nonnull begin

// === For-loop increment under condition narrowing ===

void test_for_loop_linked_list(Node* _Nullable head) {
    for (Node* _Nullable p = head; p; p = p->next) {
        p->value = 0; // OK - p narrowed from condition
    }
}

void test_for_loop_simple_increment(Node* _Nullable p) {
    for (; p; p = p->next) {
        p->value = 0; // OK
    }
}

#pragma clang assume_nonnull end
