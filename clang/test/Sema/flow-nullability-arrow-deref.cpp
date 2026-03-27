// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Entity {
    int x;
    int value() const { return x; }
};

Entity* _Nullable getHead();
Entity* _Nullable getChest();

#pragma clang assume_nonnull begin

void test_arrow_deref_warns(Entity* _Nullable p) {
    p->x = 1;              // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    int v = p->value();     // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_arrow_after_null_check(Entity* _Nullable p) {
    if (p) {
        p->x = 1;          // OK - narrowed to nonnull
        int v = p->value(); // OK - narrowed to nonnull
    }
}

void test_arrow_no_check() {
    Entity* head = getHead();
    head->x = 1;            // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_arrow_with_check() {
    Entity* head = getHead();
    if (!head) return;
    head->x = 1;            // OK - narrowed to nonnull
}

void test_star_still_works(Entity* _Nullable p) {
    (*p).x = 1;             // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_star_after_check(Entity* _Nullable p) {
    if (p) {
        (*p).x = 1;         // OK - narrowed to nonnull
    }
}

#pragma clang assume_nonnull end
