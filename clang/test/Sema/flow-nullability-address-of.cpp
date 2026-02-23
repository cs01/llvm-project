// Tests that address-of (&) expressions produce _Nonnull pointers.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Entity {
    int x;
};

int * _Nullable getNullableInt();
Entity * _Nullable getNullableEntity();

#pragma clang assume_nonnull begin

void test_addr_of_local() {
    int x = 0;
    int *p = &x;
    *p = 1; // OK - &x is nonnull
}

void test_addr_of_direct() {
    Entity e;
    Entity *p = &e;
    p->x = 1; // OK - &e is nonnull
}

void test_addr_of_member(Entity *_Nonnull obj) {
    int *p = &(obj->x);
    *p = 1; // OK - &(obj->x) is nonnull
}

void test_reassign_nullable_warns() {
    int x = 0;
    int *p = &x;
    *p = 1; // OK - initially nonnull
    p = getNullableInt();
    *p = 2; // expected-warning{{dereferencing nullable pointer}}
}

void test_nullable_control() {
    Entity *e = getNullableEntity();
    e->x = 1; // expected-warning{{dereferencing nullable pointer}}
}

#pragma clang assume_nonnull end
