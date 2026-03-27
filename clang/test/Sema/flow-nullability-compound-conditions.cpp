// Tests for complex boolean expressions in null guards.
// The CFG decomposes && and || into separate blocks. getTerminalCondition()
// follows the RHS to find the leaf condition. This tests the full range
// of compound condition patterns.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 %s -verify

struct Node {
    int value;
    Node * _Nullable next;
    Node * _Nullable child;
};

int getInt();
Node * _Nullable getNode();

#pragma clang assume_nonnull begin

// === p && q — both narrowed in body ===

void test_and_both_narrowed(Node * _Nullable p, Node * _Nullable q) {
    if (p && q) {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

// === p || q — neither narrowed in body ===

void test_or_neither_narrowed(Node * _Nullable p, Node * _Nullable q) {
    if (p || q) {
        // Can't tell which one is non-null
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
        (void)q->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === !p || !q early return — both narrowed after ===

void test_demorgan_return(Node * _Nullable p, Node * _Nullable q) {
    if (!p || !q) return;
    // De Morgan: both must be non-null to reach here
    (void)p->value; // OK
    (void)q->value; // OK
}

// === p && p->next — chained narrowing ===

void test_chain_narrowing(Node * _Nullable p) {
    if (p && p->next) {
        (void)p->value; // OK — p narrowed by first condition
        (void)p->next->value; // OK — p->next narrowed by second condition
    }
}

// === Triple && ===

void test_triple_and(Node * _Nullable a, Node * _Nullable b, Node * _Nullable c) {
    if (a && b && c) {
        (void)a->value; // OK
        (void)b->value; // OK
        (void)c->value; // OK
    }
}

// === Negated && — both narrowed after early return ===

void test_negated_and(Node * _Nullable p, Node * _Nullable q) {
    if (!(p && q)) return;
    // De Morgan equivalent: both must be non-null to reach here
    (void)p->value; // OK
    (void)q->value; // OK
}

// === != nullptr with && ===

void test_ne_null_and(Node * _Nullable p, Node * _Nullable q) {
    if (p != nullptr && q != nullptr) {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

// === == nullptr with || early return ===

void test_eq_null_or_return(Node * _Nullable p, Node * _Nullable q) {
    if (p == nullptr || q == nullptr) return;
    (void)p->value; // OK
    (void)q->value; // OK
}

// === Mixed condition: pointer check && value check ===

void test_mixed_condition(Node * _Nullable p) {
    if (p && p->value > 0) {
        (void)p->value; // OK — p narrowed by first part of &&
    }
}

// === Condition with function call ===

bool isValid(Node * _Nonnull p);

void test_condition_with_call(Node * _Nullable p) {
    if (p && isValid(p)) {
        (void)p->value; // OK — p narrowed by first part of &&
    }
}

// === Nested && in while ===

void test_while_and(Node * _Nullable p) {
    while (p && p->next) {
        (void)p->value; // OK
        p = p->next; // p = potentially nullable, but while re-checks
    }
}

// === Short-circuit in for condition ===

void test_for_and_condition(Node * _Nullable p) {
    for (int i = 0; p && i < 10; i++) {
        (void)p->value; // OK — narrowed by for condition
    }
}

// === Ternary with null check ===

void test_ternary_null_check(Node * _Nullable p) {
    int v = p ? p->value : -1; // OK — p narrowed in true branch
}

// === Multiple ternaries ===

void test_multi_ternary(Node * _Nullable a, Node * _Nullable b) {
    int v = a ? a->value : (b ? b->value : 0); // OK — each narrowed in its branch
}

// === Boolean intermediary from null check ===

void test_bool_intermediary(Node * _Nullable p) {
    bool valid = (p != nullptr);
    if (valid) {
        (void)p->value; // OK — tracked through bool guard
    }
}

// === Boolean intermediary: equality check ===

void test_bool_eq_null(Node * _Nullable p) {
    bool isNull = (p == nullptr);
    if (!isNull) {
        (void)p->value; // OK — !isNull means p is non-null
    }
}

// === Boolean intermediary: pointer truthiness ===

void test_bool_truthiness(Node * _Nullable p) {
    bool valid = p;
    if (valid) {
        (void)p->value; // OK — valid means p is truthy (non-null)
    }
}

// === Boolean intermediary: negated pointer ===

void test_bool_negated_ptr(Node * _Nullable p) {
    bool isNull = !p;
    if (!isNull) {
        (void)p->value; // OK — !isNull means p is non-null
    }
    if (isNull) {
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === Boolean intermediary invalidated by pointer reassignment ===

void test_bool_ptr_reassigned(Node * _Nullable p, Node * _Nullable q) {
    bool valid = (p != nullptr);
    p = q; // reassign p — guard is now stale
    if (valid) {
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === Boolean intermediary invalidated by bool reassignment ===

void test_bool_reassigned(Node * _Nullable p) {
    bool valid = (p != nullptr);
    valid = false; // reassign bool — guard is gone
    if (valid) {
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// === Negated && with triple condition ===

void test_negated_triple_and(Node * _Nullable a, Node * _Nullable b, Node * _Nullable c) {
    if (!(a && b && c)) return;
    (void)a->value; // OK
    (void)b->value; // OK
    (void)c->value; // OK
}

// === Negated && in if-else (body should NOT narrow) ===

void test_negated_and_body(Node * _Nullable p, Node * _Nullable q) {
    if (!(p && q)) {
        // At least one is null — can't narrow either
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    } else {
        // Both non-null
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

#pragma clang assume_nonnull end
