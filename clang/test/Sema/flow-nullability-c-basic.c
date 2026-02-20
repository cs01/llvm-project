// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable %s -verify

struct Node {
    int value;
    struct Node* _Nullable next;
};

struct Node* _Nullable getNode(void);

void test_star_deref_warns(int* p) {
    *p = 42; // expected-warning{{dereferencing nullable pointer}}
}

void test_star_after_check(int* p) {
    if (p) {
        *p = 42; // OK - narrowed
    }
}

void test_arrow_deref_warns(struct Node* p) {
    p->value = 1; // expected-warning{{dereferencing nullable pointer}}
}

void test_arrow_after_check(struct Node* p) {
    if (p) {
        p->value = 1; // OK
    }
}

void test_early_return(struct Node* p) {
    if (!p) return;
    p->value = 1; // OK - narrowed by early return
}

void test_null_comparison(struct Node* p) {
    if (p != 0) {
        p->value = 1; // OK
    }
}

void test_linked_list(struct Node* _Nullable head) {
    for (struct Node* _Nullable p = head; p; p = p->next) {
        p->value = 0; // OK
    }
}
