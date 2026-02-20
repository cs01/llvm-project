// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
struct Entity {
    int x;
};

Entity* _Nullable getEntity();

#pragma clang assume_nonnull begin

// Reassignment invalidates narrowing — the CFG-based analysis tracks this correctly.

void test_reassign_invalidates(Entity* _Nullable p, Entity* _Nullable other) {
    if (p) {
        p = other;
        (void)*p; // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
    }
}

void test_deref_before_reassign(Entity* _Nullable p, Entity* _Nullable other) {
    if (p) {
        (*p).x = 1; // OK - narrowed
        p = other;
        (*p).x = 2; // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
    }
}

void test_reassign_then_recheck(Entity* _Nullable p) {
    p = getEntity();
    if (p) {
        (*p).x = 1; // OK - re-narrowed after reassignment
    }
}

void test_increment_invalidates(Entity* _Nullable p) {
    if (p) {
        p++;
        (void)*p; // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
    }
}

#pragma clang assume_nonnull end
