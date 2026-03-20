// Tests that explicit casts preserve nullability from the source operand.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Base {
    int x;
};

struct Derived : Base {
    int y;
};

Base * _Nullable getNullable();
Base * _Nonnull getNonnull();

#pragma clang assume_nonnull begin

void test_c_style_cast_nonnull() {
    Base *b = getNonnull();
    Derived *d = (Derived *)b;
    d->y = 1; // OK - nonnull propagated through C-style cast
}

void test_static_cast_nonnull() {
    Base *b = getNonnull();
    Derived *d = static_cast<Derived *>(b);
    d->y = 1; // OK - nonnull propagated through static_cast
}

void test_reinterpret_cast_nonnull() {
    Base *b = getNonnull();
    int *ip = reinterpret_cast<int *>(b);
    *ip = 1; // OK - nonnull propagated through reinterpret_cast
}

void test_c_style_cast_nullable_warns() {
    Base *b = getNullable();
    Derived *d = (Derived *)b;
    d->y = 1; // expected-warning{{dereferencing nullable pointer}}
}

void test_explicit_nonnull_dest() {
    Base *b = getNullable();
    Derived * _Nonnull d = (Derived * _Nonnull)b;
    d->y = 1; // OK - explicit _Nonnull on dest type
}

// --- reinterpret_cast on this + pointer arithmetic ---

typedef unsigned char uint8_t;

struct Foo {
    int x;
    void test_cast_this() {
        auto* p = reinterpret_cast<uint8_t*>(this) + 4;
        *p = 0; // OK — this is always non-null, arithmetic preserves it
    }
};

void test_ptr_arith_nonnull(int* p) {
    auto* q = p + 1;
    *q = 0; // OK — p is nonnull (assume_nonnull), arithmetic preserves it
}

void test_ptr_arith_nullable(int* _Nullable p) {
    auto* q = p + 1;
    *q = 0; // expected-warning{{dereferencing nullable pointer}}
}

#pragma clang assume_nonnull end
