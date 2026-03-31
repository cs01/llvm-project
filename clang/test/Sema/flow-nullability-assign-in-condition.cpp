// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -std=c++17 %s -verify

// Test that assignment-in-condition patterns narrow the assigned variable.
// e.g., while ((p = get()) != nullptr) { *p; }

#pragma clang assume_nonnull begin

struct Node {
    int value;
    Node *_Nullable next;
};

Node *_Nullable get_next(Node *n);

void while_assign_ne_null(Node *_Nullable head) {
    Node *p;
    while ((p = get_next(head)) != nullptr) { // expected-warning 2{{passing nullable pointer to nonnull parameter}} expected-note 2{{add a null check before the call}}
        p->value = 1; // OK — p narrowed by != nullptr
    }
}

void while_assign_truthiness(Node *_Nullable head) {
    Node *p;
    while ((p = get_next(head))) { // expected-warning 2{{passing nullable pointer to nonnull parameter}} expected-note 2{{add a null check before the call}}
        p->value = 1; // OK — p narrowed by truthiness
    }
}

void if_assign_ne_null(Node *_Nullable head) {
    Node *p;
    if ((p = get_next(head)) != nullptr) { // expected-warning{{passing nullable pointer to nonnull parameter}} expected-note{{add a null check before the call}}
        p->value = 1; // OK — p narrowed
    }
}

void for_assign_ne_null(Node *_Nullable head) {
    for (Node *p; (p = get_next(head)) != nullptr;) { // expected-warning 2{{passing nullable pointer to nonnull parameter}} expected-note 2{{add a null check before the call}}
        p->value = 1; // OK — p narrowed
    }
}

#pragma clang assume_nonnull end
