// Tests for lambda capture interactions with flow-sensitive nullability.
// Lambdas create separate function bodies — the analysis is intraprocedural,
// so each lambda body is analyzed independently.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
};

Node * _Nullable getNode();

#pragma clang assume_nonnull begin

// === Capture nullable by value — warns inside lambda ===

void test_capture_nullable_by_value(Node * _Nullable p) {
    auto f = [p]() {
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    };
    f();
}

// === Capture narrowed by value — still nullable inside lambda ===
// Even though p was narrowed before the lambda, the capture creates a new
// copy. The analysis treats each function body independently.

void test_capture_narrowed_by_value(Node * _Nullable p) {
    if (p) {
        auto f = [p]() {
            // p is captured by value from narrowed context, but the lambda
            // is a separate function body. The analysis sees p as the
            // lambda's parameter (implicitly nullable in nullable-default).
            (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
        };
        f();
        (void)p->value; // OK — still narrowed in outer scope
    }
}

// === Capture by reference — narrowing does not propagate ===

void test_capture_by_ref(Node * _Nullable p) {
    if (p) {
        auto f = [&p]() {
            (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
        };
        f();
    }
}

// === Lambda with its own null check ===

void test_lambda_own_check(Node * _Nullable p) {
    auto f = [p]() {
        if (p)
            (void)p->value; // OK — narrowed inside lambda
    };
    f();
}

// === Immediately-invoked lambda expression ===

void test_iife(Node * _Nullable p) {
    [p]() {
        if (p)
            (void)p->value; // OK — narrowed
    }();
}

// === Lambda capturing nonnull pointer ===

void test_capture_nonnull(Node * _Nonnull p) {
    auto f = [p]() {
        (void)p->value; // OK — _Nonnull captured
    };
    f();
}

// === Generic lambda with auto parameter ===

void test_generic_lambda() {
    auto f = [](auto * _Nullable p) {
        if (p)
            (void)p->value; // OK — narrowed
    };
    Node * _Nullable n = nullptr;
    f(n);
}

// === Lambda returning nullable pointer ===

void test_lambda_return() {
    Node * _Nullable n = nullptr;
    auto getter = [&n]() -> Node * _Nullable { return n; };
    Node * _Nullable result = getter();
    (void)result->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Nested lambdas ===

void test_nested_lambda(Node * _Nullable p) {
    auto outer = [p]() {
        auto inner = [p]() {
            (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
        };
        inner();
    };
    outer();
}

// === Lambda with no captures — unrelated pointer ===

void test_lambda_no_capture() {
    auto f = [](Node * _Nullable p) {
        if (!p) return;
        (void)p->value; // OK — narrowed by early return
    };
    f(nullptr);
}

// === Mutable lambda modifying captured pointer ===

void test_mutable_capture(Node * _Nullable p) {
    auto f = [p]() mutable {
        p = nullptr; // mutate the captured copy
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    };
    f();
}

// === Init-capture (C++14) — captures are independent variables ===

void test_init_capture_warns() {
    auto f = [p = getNode()]() {
        (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    };
    f();
}

void test_init_capture_with_check() {
    auto f = [p = getNode()]() {
        if (p)
            (void)p->value; // OK — narrowed inside lambda
    };
    f();
}

#pragma clang assume_nonnull end
