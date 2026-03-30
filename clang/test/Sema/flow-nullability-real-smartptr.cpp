// Tests that flow-sensitive nullability works with real standard library headers,
// not just mocked std:: types. This catches AST wrapping differences
// (ExprWithCleanups, CXXBindTemporaryExpr) that mocks don't produce.
//
// This test requires system C++ headers, so it runs through the driver
// rather than cc1. It is unsupported on targets without a C++ stdlib.
// UNSUPPORTED: target={{.*-windows.*}}
// REQUIRES: system-darwin || system-linux
// RUN: %clangxx -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -Xclang -verify

#include <memory>

struct Node {
    int value;
};

#pragma clang assume_nonnull begin

void make_unique_narrows() {
    auto sp = std::make_unique<Node>();
    sp->value = 1; // OK — make_unique always returns non-null
}

void make_shared_narrows() {
    auto sp = std::make_shared<Node>();
    sp->value = 1; // OK — make_shared always returns non-null
}

void move_makes_nullable() {
    auto sp = std::make_unique<Node>();
    sp->value = 1; // OK
    auto other = std::move(sp);
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void reassign_make_unique_renarrows() {
    std::unique_ptr<Node> sp;
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    sp = std::make_unique<Node>();
    sp->value = 1; // OK — reassignment from make_unique narrows
}

#pragma clang assume_nonnull end
