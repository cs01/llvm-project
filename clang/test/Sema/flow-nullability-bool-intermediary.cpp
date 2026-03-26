// Tests for boolean intermediary narrowing — tracking null-check results
// stored in bool variables, and for !(p && q) decomposition.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 %s -verify

struct Node {
    int value;
    Node * _Nullable next;
};

#pragma clang assume_nonnull begin

// === Basic patterns ===

void test_ne_nullptr(Node * _Nullable p) {
    bool valid = (p != nullptr);
    if (valid) {
        (void)p->value; // OK
    }
    // Outside the if, p is still nullable
    (void)p->value; // expected-warning{{dereferencing nullable pointer}}
}

void test_eq_nullptr_negated(Node * _Nullable p) {
    bool isNull = (p == nullptr);
    if (!isNull) {
        (void)p->value; // OK
    }
}

void test_pointer_truthiness(Node * _Nullable p) {
    bool valid = p;
    if (valid) {
        (void)p->value; // OK
    }
}

void test_negated_pointer(Node * _Nullable p) {
    bool isNull = !p;
    if (!isNull) {
        (void)p->value; // OK
    }
}

// === Invalidation ===

void test_pointer_reassigned(Node * _Nullable p, Node * _Nullable q) {
    bool valid = (p != nullptr);
    p = q; // reassign pointer — bool guard is stale
    if (valid) {
        (void)p->value; // expected-warning{{dereferencing nullable pointer}}
    }
}

void test_bool_reassigned(Node * _Nullable p) {
    bool valid = (p != nullptr);
    valid = false;
    if (valid) {
        (void)p->value; // expected-warning{{dereferencing nullable pointer}}
    }
}

void test_pointer_incremented(int * _Nullable p) {
    bool valid = (p != nullptr);
    p++;
    if (valid) {
        (void)*p; // expected-warning{{dereferencing nullable pointer}}
    }
}

// === Negated conjunction ===

void test_negated_and_return(Node * _Nullable p, Node * _Nullable q) {
    if (!(p && q)) return;
    (void)p->value; // OK
    (void)q->value; // OK
}

void test_negated_and_else(Node * _Nullable p, Node * _Nullable q) {
    if (!(p && q)) {
        (void)p->value; // expected-warning{{dereferencing nullable pointer}}
    } else {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

void test_negated_triple_and(Node * _Nullable a, Node * _Nullable b, Node * _Nullable c) {
    if (!(a && b && c)) return;
    (void)a->value; // OK
    (void)b->value; // OK
    (void)c->value; // OK
}

void test_negated_and_ne_nullptr(Node * _Nullable p, Node * _Nullable q) {
    if (!(p != nullptr && q != nullptr)) return;
    (void)p->value; // OK
    (void)q->value; // OK
}

// === Combined: bool guard + negated && ===

void test_bool_guard_in_and(Node * _Nullable p, Node * _Nullable q) {
    bool pOk = (p != nullptr);
    if (pOk && q) {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

// === Bool guard does not track compound conditions ===

void test_bool_compound_not_tracked(Node * _Nullable p, Node * _Nullable q) {
    bool both = (p && q);
    if (both) {
        // Compound conditions are not decomposed into per-variable guards
        (void)p->value; // expected-warning{{dereferencing nullable pointer}}
        (void)q->value; // expected-warning{{dereferencing nullable pointer}}
    }
}

#pragma clang assume_nonnull end
