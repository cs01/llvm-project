// Tests for complex control flow graph patterns and intersect semantics.
// The analysis intersects narrowing at merge points — a variable is only
// narrowed after a merge if ALL incoming paths agree it's narrowed.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 %s -verify

struct Node {
    int value;
    Node * _Nullable left;
    Node * _Nullable right;
};

Node * _Nullable getNode();
Node * _Nonnull getSafeNode();
int getInt();

#pragma clang assume_nonnull begin

// === Diamond: both branches narrow — merge keeps narrowing ===

void test_diamond_both_narrow(Node * _Nullable p) {
    if (getInt()) {
        if (!p) return;
    } else {
        if (!p) return;
    }
    (void)p->value; // OK — narrowed on both paths
}

// === Diamond: only one branch narrows — merge loses narrowing ===

void test_diamond_one_narrows(Node * _Nullable p) {
    if (getInt()) {
        if (!p) return;
        // narrowed here
    } else {
        // NOT narrowed here
    }
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Diamond: both branches assign nonnull ===

void test_diamond_assign_both(int * _Nullable p) {
    int x = 0, y = 0;
    if (getInt()) {
        p = &x;
    } else {
        p = &y;
    }
    (void)*p; // OK — both branches assign nonnull (address-of)
}

// === Diamond: one branch assigns nonnull, other doesn't touch ===

void test_diamond_assign_one(Node * _Nullable p) {
    int x;
    if (getInt()) {
        // p unchanged — still nullable
    } else {
        if (!p) return;
    }
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Nested if-else (3 levels) ===

void test_nested_three_levels(Node * _Nullable p, Node * _Nullable q) {
    if (!p) return;
    if (getInt()) {
        if (!q) return;
        (void)p->value; // OK
        (void)q->value; // OK
    } else {
        (void)p->value; // OK — outer guard still holds
        (void)q->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === Loop with multiple exit points ===

void test_loop_multi_exit(Node * _Nullable p) {
    for (int i = 0; i < 10; i++) {
        if (!p) break;
        (void)p->value; // OK — narrowed by break guard
    }
    // After loop, p may or may not be narrowed (break path vs normal exit)
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Sequential narrowing ===

void test_sequential_narrow(Node * _Nullable p, Node * _Nullable q, Node * _Nullable r) {
    if (!p) return;
    if (!q) return;
    if (!r) return;
    (void)p->value; // OK
    (void)q->value; // OK
    (void)r->value; // OK
}

// === Narrowing lost after reassignment in one branch ===

void test_reassign_in_branch(Node * _Nullable p) {
    if (!p) return;
    // p is narrowed
    if (getInt()) {
        p = getNode(); // reassigned to nullable
    }
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Do-while loop ===

void test_do_while(Node * _Nullable p) {
    if (!p) return;
    do {
        (void)p->value; // OK — narrowed on entry, loop back-edge preserves
    } while (getInt() && p);
}

// === Nested loops ===

void test_nested_loops(Node * _Nullable p) {
    if (!p) return;
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            (void)p->value; // OK — narrowed, loops don't invalidate
        }
    }
}

// === Switch with fallthrough ===

void test_switch_fallthrough(Node * _Nullable p) {
    switch (getInt()) {
    case 0:
        if (!p) return;
        // fallthrough — narrowing from case 0
        [[fallthrough]];
    case 1:
        // Reached from case 0 (narrowed) OR case 1 (not narrowed)
        // Intersect: NOT narrowed
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
        break;
    default:
        break;
    }
}

// === Post-loop narrowing via break vs normal exit ===

void test_post_loop_narrowing(Node * _Nullable p) {
    while (true) {
        if (p) break; // exit loop only when p is non-null
        p = getNode();
    }
    // After loop, we only exit via break where p was non-null
    (void)p->value; // OK — only exit is via break where p is narrowed
}

// === if-else with return in both branches ===

Node * _Nonnull test_both_return(Node * _Nullable p) {
    if (p) {
        return p; // OK
    } else {
        return getSafeNode();
    }
}

// === Ternary chain ===

void test_ternary_chain(Node * _Nullable a, Node * _Nullable b, Node * _Nullable c) {
    Node *picked = a ? a : (b ? b : c);
    // picked may be c which is nullable
    if (picked)
        (void)picked->value; // OK — narrowed
}

// === Back-edge invalidation in while loop ===

void test_while_reassign(Node * _Nullable p) {
    while (p) {
        (void)p->value; // OK — narrowed by while condition
        p = p->left; // reassign — p may become null
        // Back-edge: p is now potentially null, but while condition re-checks
    }
}

#pragma clang assume_nonnull end
