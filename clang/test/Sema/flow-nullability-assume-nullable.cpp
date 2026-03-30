// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -std=c++17 %s -verify

struct Node {
    int value;
    Node *next;
};

// === Basic: unchecked deref warns, checked deref is OK ===

#pragma clang assume_nullable begin

void unchecked_deref(Node *p) {
    p->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void null_checked(Node *p) {
    if (p) {
        p->value = 1; // OK
    }
}

void early_return(Node *p) {
    if (!p) return;
    p->value = 1; // OK
}

// === _Nonnull opts out of nullable default ===

void nonnull_param(Node * _Nonnull p) {
    p->value = 1; // OK
}

// === Mixed: some params nullable, some nonnull ===

void mixed(Node *a, Node * _Nonnull b) {
    a->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    b->value = 1; // OK
}

// === Chained access ===

void chained(Node *p) {
    if (p && p->next) {
        p->next->value = 1; // OK
    }
}

#pragma clang assume_nullable end

// === Outside pragma: no warnings ===

void outside_pragma(Node *p) {
    p->value = 1; // OK — outside pragma, no nullability inference
}

// === Pragma nesting error ===

#pragma clang assume_nullable begin // expected-note {{#pragma entered here}}
#pragma clang assume_nullable begin // expected-error {{already inside '#pragma clang assume_nullable'}}
#pragma clang assume_nullable end

// === Unmatched end ===

#pragma clang assume_nullable end // expected-error {{not currently inside '#pragma clang assume_nullable'}}

// === Conflict with assume_nonnull ===

#pragma clang assume_nonnull begin // expected-note {{#pragma entered here}}
#pragma clang assume_nullable begin // expected-error {{cannot use '#pragma clang assume_nullable' inside '#pragma clang assume_nonnull'}}
#pragma clang assume_nonnull end
