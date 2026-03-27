// Tests for if constexpr interaction with flow-sensitive nullability.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

#pragma clang assume_nonnull begin

// Known limitation: the warn_null_init_nonnull check fires during
// declaration processing, before if-constexpr discarding. This means
// _Nonnull p = nullptr in a discarded branch still warns. Suppressing
// this would require tracking discarded-branch state at decl processing
// time, which Clang doesn't expose. In practice, writing explicit
// _Nonnull p = nullptr in a discarded branch is very rare.

void test_if_constexpr_discarded() {
    if constexpr (false) {
        int * _Nonnull p = nullptr; // expected-warning{{null assigned to a variable of nonnull type}}
    }
}

// Live branch correctly warns
void test_if_constexpr_live() {
    if constexpr (true) {
        int * _Nonnull p = nullptr; // expected-warning{{null assigned to a variable of nonnull type}}
    }
}

// Flow analysis narrowing works in live constexpr branches
void test_if_constexpr_narrowing(int * _Nullable p) {
    if constexpr (true) {
        if (p) {
            *p = 42; // OK — narrowed
        }
    }
}

// Dereference in live branch warns correctly
void test_if_constexpr_deref(int * _Nullable p) {
    if constexpr (true) {
        *p = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }
}

// Template with if constexpr — both instantiations checked
template<bool B>
void template_constexpr_branch() {
    if constexpr (B) {
        int * _Nonnull p = nullptr; // expected-warning 2{{null assigned to a variable of nonnull type}}
    }
}

void instantiate_both() {
    template_constexpr_branch<false>();
    template_constexpr_branch<true>(); // expected-note{{in instantiation}}
}

#pragma clang assume_nonnull end
