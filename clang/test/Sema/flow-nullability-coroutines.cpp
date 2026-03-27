// Tests for coroutine interactions with flow-sensitive nullability analysis.
// Coroutines introduce suspension points where control flow is non-obvious.
// The analysis should handle co_await/co_yield/co_return without crashing
// and without producing spurious warnings.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++20 -I%S/../SemaCXX/Inputs %s -verify
// expected-no-diagnostics

#include "std-coroutine.h"

struct Node {
    int value;
    Node * _Nullable next;
};

// --- Generator coroutine type ---

struct Generator {
    struct promise_type {
        Node * _Nullable current;
        Generator get_return_object() { return {}; }
        std::suspend_always initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() {}
        std::suspend_always yield_value(Node * _Nullable val) {
            current = val;
            return {};
        }
        void return_void() {}
    };
};

// --- Task coroutine type ---

struct Task {
    struct promise_type {
        Task get_return_object() { return {}; }
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void unhandled_exception() {}
        void return_void() {}
    };
};

// --- Awaitable that returns a nullable pointer ---

struct NullableAwaitable {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    Node * _Nullable await_resume() const noexcept { return nullptr; }
};

#pragma clang assume_nonnull begin

// === Basic coroutine with nullable check ===

Generator yield_nodes(Node * _Nullable head) {
    for (Node * _Nullable p = head; p; p = p->next) {
        (void)p->value; // OK — narrowed by loop condition
        co_yield p;
    }
}

// === co_await returning nullable ===

Task consume_awaitable() {
    NullableAwaitable awaitable;
    Node * _Nullable result = co_await awaitable;
    if (result) {
        (void)result->value; // OK — narrowed
    }
    co_return;
}

// === Null check before co_yield ===

Generator guarded_yield(Node * _Nullable n) {
    if (n) {
        (void)n->value; // OK — narrowed
        co_yield n;
        (void)n->value; // OK — still narrowed (no reassignment)
    }
}

// === Multiple co_yields with independent checks ===

Generator multi_yield(Node * _Nullable a, Node * _Nullable b) {
    if (a) {
        co_yield a;
    }
    if (b) {
        co_yield b;
    }
}

// === Coroutine with nonnull parameter ===

Generator nonnull_param(Node * _Nonnull n) {
    (void)n->value; // OK — _Nonnull
    co_yield n;
    (void)n->value; // OK — _Nonnull
}

#pragma clang assume_nonnull end
