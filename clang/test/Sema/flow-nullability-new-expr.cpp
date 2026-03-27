// Tests that throwing operator new is treated as _Nonnull (it never returns null),
// while nothrow operator new is left nullable.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

typedef __SIZE_TYPE__ size_t;

namespace std {
  struct nothrow_t {};
  extern const nothrow_t nothrow;
}

void *operator new(size_t, const std::nothrow_t &) noexcept;

struct Widget {
    int value;
};

Widget * _Nullable getNullableWidget();

#pragma clang assume_nonnull begin

void test_new_direct_deref() {
    Widget *w = new Widget();
    w->value = 42; // OK - throwing new never returns null
}

void test_new_var_deref() {
    Widget *w = new Widget();
    int v = w->value; // OK - narrowed via new
}

void test_nothrow_new_warns() {
    Widget *w = new (std::nothrow) Widget();
    w->value = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_nullable_control() {
    Widget *w = getNullableWidget();
    w->value = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

#pragma clang assume_nonnull end
