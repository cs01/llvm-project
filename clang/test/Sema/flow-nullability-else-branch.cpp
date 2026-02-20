// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Entity {
    int x;
};

#pragma clang assume_nonnull begin

// === Else-branch narrowing for single negated check ===

void test_else_simple(Entity* _Nullable p) {
    if (!p) {
        return;
    } else {
        p->x = 1; // OK - narrowed in else branch
    }
}

// === Else-branch narrowing for compound OR conditions ===

void test_else_or_two_vars(Entity* _Nullable p, Entity* _Nullable q) {
    if (!p || !q) {
        return;
    } else {
        p->x = q->x; // OK - both narrowed in else branch
    }
}

void test_else_or_three_vars(Entity* _Nullable p, Entity* _Nullable q, Entity* _Nullable r) {
    if (!p || !q || !r) {
        return;
    } else {
        p->x = q->x + r->x; // OK - all narrowed in else branch
    }
}

// === Early-return (no else) still works for OR ===

void test_early_return_or(Entity* _Nullable p, Entity* _Nullable q) {
    if (!p || !q)
        return;
    p->x = q->x; // OK - both narrowed after early return
}

// === Positive check with else should NOT narrow in else ===

void test_positive_else_no_narrow(Entity* _Nullable p) {
    if (p) {
        p->x = 1; // OK - narrowed in then branch
    } else {
        p->x = 2; // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
    }
}

// === Member expression narrowing in else ===

void test_else_member_narrowing(Entity* _Nullable p) {
    if (!p) {
        // p is null here
    } else {
        p->x = 1; // OK - narrowed in else
    }
}

#pragma clang assume_nonnull end
