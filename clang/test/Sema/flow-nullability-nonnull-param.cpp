// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Entity {
    int x;
    int value() const { return x; }
};

#pragma clang assume_nonnull begin

void test_nonnull_star(Entity* _Nonnull p) {
    (*p).x = 1; // OK - _Nonnull never warns
}

void test_nonnull_arrow(Entity* _Nonnull p) {
    p->x = 1; // OK - _Nonnull never warns
}

void test_nonnull_method(Entity* _Nonnull p) {
    int v = p->value(); // OK
}

void test_nonnull_local() {
    Entity e;
    Entity* _Nonnull p = &e;
    p->x = 1; // OK - _Nonnull local
}

void test_mixed_params(Entity* _Nonnull safe, Entity* _Nullable risky) {
    safe->x = 1; // OK - _Nonnull
    if (risky) {
        risky->x = safe->x; // OK - risky narrowed, safe is _Nonnull
    }
}

void test_nonnull_after_null_check(Entity* _Nonnull p) {
    if (p) {
        p->x = 1; // OK - redundant check, but still fine
    }
    p->x = 2; // OK - _Nonnull
}

#pragma clang assume_nonnull end
