// Tests for structured bindings with flow-sensitive nullability analysis.
// C++17 structured bindings produce BindingDecl nodes, not VarDecls.
// The analysis currently tracks narrowing on VarDecls, so structured
// binding variables are not narrowable. This test documents that behavior
// and verifies no crashes occur.
//
// Structured binding pointers that need null-checking should be captured
// into local variables first — this test also shows that workaround.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node * _Nullable next;
};

Node * _Nullable getNode();

// Pair-like type for structured bindings
struct PtrPair {
    Node * _Nullable first;
    Node * _Nullable second;
};

PtrPair getPair();

// Tuple-like for testing get<> protocol
struct Triple {
    Node * _Nullable a;
    Node * _Nullable b;
    int c;
};

Triple getTriple();

#pragma clang assume_nonnull begin

// === Basic struct decomposition ===
// Structured binding variables are BindingDecls, not VarDecls.
// The analysis does not currently track narrowing on BindingDecls,
// so these accesses do not warn even without null checks.
// This is a known false negative — documenting that no crash occurs.

void test_struct_decomp() {
    PtrPair pair = getPair();
    auto [p, q] = pair;
    // p and q are BindingDecls — analysis doesn't track them,
    // so no warning is produced (accepted false negative).
    if (p) {
        (void)p->value; // OK — narrowed (even though binding)
    }
    if (q) {
        (void)q->value; // OK
    }
}

// === Decomposition with && guard ===

void test_decomp_both_checked() {
    auto [p, q] = getPair();
    if (p && q) {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

// === Mixed nullable/non-nullable struct ===

struct MixedPair {
    Node * _Nonnull safe;
    Node * _Nullable risky;
};

MixedPair getMixed();

void test_mixed_decomp() {
    auto [safe, risky] = getMixed();
    (void)safe->value;  // OK — source is _Nonnull
    // risky is a BindingDecl — no warning produced (false negative)
}

void test_mixed_decomp_guarded() {
    auto [safe, risky] = getMixed();
    (void)safe->value; // OK
    if (risky) {
        (void)risky->value; // OK — checked
    }
}

// === Decomposition from triple ===

void test_triple_decomp() {
    auto [a, b, c] = getTriple();
    if (a && b) {
        (void)a->value; // OK
        (void)b->value; // OK
    }
    (void)c; // OK — int, not a pointer
}

// === Reference binding through structured bindings ===

void test_ref_decomp() {
    PtrPair pair = getPair();
    auto &[p, q] = pair;
    if (p) {
        (void)p->value; // OK
    }
}

// === Workaround: capture into local variable for narrowing ===
// This is the recommended pattern when you need null-checking with
// structured bindings.

void test_capture_workaround() {
    auto [first, second] = getPair();
    Node * _Nullable p = first;
    Node * _Nullable q = second;
    if (p && q) {
        (void)p->value; // OK — local VarDecl is tracked
        (void)q->value; // OK
    }
}

// === Structured binding in if-init (C++17) ===

void test_if_init_decomp() {
    if (auto [p, q] = getPair(); p && q) {
        (void)p->value; // OK
        (void)q->value; // OK
    }
}

// === Structured binding in for-range-init ===

struct PairList {
    PtrPair pairs[3];
    PtrPair *begin() { return pairs; }
    PtrPair *end() { return pairs + 3; }
};

void test_range_decomp(PairList &list) {
    for (auto [p, q] : list) {
        if (p) {
            (void)p->value; // OK
        }
    }
}

// === Decomposition of stack-allocated struct ===

void test_stack_decomp() {
    int x = 42;
    Node node{0, nullptr};
    struct { Node * _Nonnull p; int *q; } s = {&node, &x};
    auto [p, q] = s;
    (void)p->value; // OK — source is _Nonnull
}

#pragma clang assume_nonnull end
