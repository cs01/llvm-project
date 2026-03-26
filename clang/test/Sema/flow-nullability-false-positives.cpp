// False positive regression suite for flow-sensitive nullability analysis.
// Every test case in this file must produce NO warnings. These represent
// common C++ patterns that an overly-aggressive analysis might flag.
// This file is the most important for reviewer confidence.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Node {
    int value;
    Node * _Nullable next;
};

int getInt();
Node * _Nullable getNode();

#pragma clang assume_nonnull begin

// === Conditional initialization on all paths ===

void test_conditional_init(bool cond) {
    int x = 0, y = 0;
    int *p;
    if (cond) {
        p = &x;
    } else {
        p = &y;
    }
    (void)*p; // OK — assigned nonnull on both paths
}

// === Static local variable ===

void test_static_local() {
    static int x = 42;
    int *p = &x;
    (void)*p; // OK — address-of is always nonnull
}

// === Global variable access ===

int g_value = 0;

void test_global_addr() {
    int *p = &g_value;
    (void)*p; // OK — address-of
}

// === Function pointer call ===

typedef int (*IntFn)(int);

void test_fn_ptr(IntFn fn) {
    // Function pointers are not subject to null dereference checking
    // in the same way — they're called, not dereferenced with *
    int result = fn(42); // OK — no * dereference
}

// === Chained method calls on nonnull ===

struct Builder {
    Builder *setX(int) { return this; }
    Builder *setY(int) { return this; }
    int build() { return 0; }
};

void test_builder_pattern() {
    Builder b;
    // this is always nonnull, so chained -> returns are fine
    b.setX(1)->setY(2)->build(); // OK — this is nonnull
}

// === Address-of array element ===

void test_array_element_addr() {
    int arr[10];
    int *p = &arr[5];
    (void)*p; // OK — address-of
}

// === Pointer to member of stack object ===

void test_member_addr() {
    Node n;
    int *p = &n.value;
    (void)*p; // OK — address-of
}

// === Ternary with nonnull on both sides ===

void test_ternary_both_nonnull(bool cond) {
    int x = 1, y = 2;
    int *p = cond ? &x : &y;
    (void)*p; // OK — nonnull on both branches
}

// === Cast of nonnull ===

void test_cast_nonnull() {
    int x = 42;
    void *vp = &x;
    int *ip = static_cast<int *>(vp);
    (void)*ip; // OK — source was nonnull (address-of)
}

// === new expression ===

void test_throwing_new() {
    int *p = new int(42);
    (void)*p; // OK — throwing new never returns null
}

// === Multiple checks, then use ===

void test_multi_check(Node * _Nullable a, Node * _Nullable b, Node * _Nullable c) {
    if (a && b && c) {
        (void)a->value; // OK
        (void)b->value; // OK
        (void)c->value; // OK
    }
}

// === Reassign to nonnull after nullable ===

void test_reassign_nonnull() {
    int x;
    int * _Nullable p = nullptr;
    p = &x;
    (void)*p; // OK — reassigned to nonnull
}

// === Loop variable always nonnull ===

void test_loop_var() {
    int arr[10];
    for (int i = 0; i < 10; i++) {
        int *p = &arr[i];
        (void)*p; // OK — address-of
    }
}

// === Nested struct access on nonnull ===

struct Outer {
    Node node;
};

void test_nested_nonnull_access() {
    Outer o;
    int v = o.node.value; // OK — dot access on stack object, no deref
}

// === Pointer arithmetic on nonnull ===

void test_ptr_arith() {
    int arr[10];
    int *p = arr;     // array decays to pointer — nonnull
    int *q = arr + 5; // arithmetic on nonnull — still nonnull
    (void)*q; // OK
}

// === Reference binding ===

void test_reference(int * _Nonnull p) {
    int &ref = *p; // OK — _Nonnull
    ref = 42;
}

// === Comma operator with pointer ===

void test_comma_op() {
    int x;
    int *p = (getInt(), &x);
    (void)*p; // OK — comma evaluates to &x which is nonnull
}

// === Narrowing survives function calls ===
// The analysis correctly does NOT invalidate narrowing on function calls
// because pointers are passed by value in C/C++.

void external_fn();

void test_narrowing_survives_call(Node * _Nullable p) {
    if (!p) return;
    external_fn();
    (void)p->value; // OK — function call doesn't invalidate narrowing
}

// === sizeof/alignof don't dereference ===

void test_sizeof_no_deref(Node * _Nullable p) {
    auto s = sizeof(*p); // OK — sizeof doesn't evaluate its operand
    (void)s;
}

// === decltype doesn't dereference ===

void test_decltype_no_deref(Node * _Nullable p) {
    using T = decltype(p->value); // OK — decltype is unevaluated
    T x = 0;
    (void)x;
}

// === this pointer in member functions ===

struct Obj {
    int x;
    void method() {
        this->x = 1; // OK — this is never null
        (*this).x = 2; // OK — *this is suppressed
    }
};

// === Non-std iterator dereference ===

struct MyIterator {
    Node *current;
    Node &operator*() { return *current; }
    Node *operator->() { return current; }
};

void test_iterator_deref(MyIterator it) {
    (void)it->value; // OK — non-std operator-> is not checked
}

#pragma clang assume_nonnull end
