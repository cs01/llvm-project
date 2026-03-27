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
        (void)*p; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

void test_deref_before_reassign(Entity* _Nullable p, Entity* _Nullable other) {
    if (p) {
        (*p).x = 1; // OK - narrowed
        p = other;
        (*p).x = 2; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

void test_reassign_then_recheck(Entity* _Nullable p) {
    p = getEntity();
    if (p) {
        (*p).x = 1; // OK - re-narrowed after reassignment
    }
}

// Pointer increment/decrement preserves narrowing — arithmetic on a
// non-null pointer is still non-null (same as p + 1 in initialization).
void test_increment_preserves_narrowing(Entity* _Nullable p) {
    if (p) {
        p++;
        (void)*p; // OK — p++ on non-null is still non-null
    }
}

void test_decrement_preserves_narrowing(Entity* _Nullable p) {
    if (p) {
        --p;
        (void)*p; // OK — --p on non-null is still non-null
    }
}

// But member narrowing IS invalidated by pointer arithmetic,
// since the pointer now points to a different object.
struct Chain {
    int value;
    Chain * _Nullable next;
};

void test_increment_invalidates_member(Chain * _Nullable p) {
    if (p && p->next) {
        p++;
        p->next->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

#pragma clang assume_nonnull end
