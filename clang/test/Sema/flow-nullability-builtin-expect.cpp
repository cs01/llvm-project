// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Entity {
    int x;
};

[[noreturn]] void fatal(const char* msg);

#pragma clang assume_nonnull begin

// === __builtin_expect in conditions ===

void test_builtin_expect_if(Entity* _Nullable p) {
    if (__builtin_expect(!!(p), 1)) {
        p->x = 1; // OK - narrowed through __builtin_expect
    }
}

void test_builtin_expect_negated(Entity* _Nullable p) {
    if (__builtin_expect(!!(p == nullptr), 0))
        return;
    p->x = 1; // OK - early return narrowing through __builtin_expect
}

void test_builtin_expect_early_return(Entity* _Nullable p) {
    if (__builtin_expect(!!(!p), 0))
        return;
    p->x = 1; // OK
}

// === Macro-wrapped __builtin_expect (like LIKELY/UNLIKELY) ===

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

void test_likely_macro(Entity* _Nullable p) {
    if (LIKELY(p)) {
        p->x = 1; // OK - narrowed
    }
}

void test_unlikely_null_check(Entity* _Nullable p) {
    if (UNLIKELY(!p))
        return;
    p->x = 1; // OK
}

// === __builtin_expect in assertion macros ===

#define CHECK(cond) do { if (__builtin_expect(!(cond), 0)) fatal("CHECK failed"); } while(0)

void test_check_macro(Entity* _Nullable p) {
    CHECK(p);
    p->x = 1; // OK - CHECK asserted non-null
}

void test_check_macro_two_vars(Entity* _Nullable p, Entity* _Nullable q) {
    CHECK(p);
    CHECK(q);
    p->x = q->x; // OK
}

// === __builtin_assume narrows pointers ===

void test_builtin_assume_simple(Entity* _Nullable p) {
    __builtin_assume(p != nullptr);
    p->x = 1; // OK - narrowed by __builtin_assume
}

void test_builtin_assume_truthiness(Entity* _Nullable p) {
    __builtin_assume(p);
    p->x = 1; // OK - narrowed by __builtin_assume(p)
}

void test_builtin_assume_two_vars(Entity* _Nullable p, Entity* _Nullable q) {
    __builtin_assume(p != nullptr);
    __builtin_assume(q != nullptr);
    p->x = q->x; // OK
}

// === Without __builtin_expect still warns ===

void test_no_narrowing_without_check(Entity* _Nullable p) {
    p->x = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

#pragma clang assume_nonnull end
