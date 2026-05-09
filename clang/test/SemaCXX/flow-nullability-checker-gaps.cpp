// TDD tests for null checker features not yet implemented.
// Each test documents the DESIRED behavior. Tests that fail reveal gaps to fix.
// Tests marked XFAIL-GAP document expected failures — remove the workaround
// annotations as each gap is closed.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 -Wno-unused-value -Wno-nonnull %s -verify

#pragma clang assume_nonnull begin

// Forward declarations
int *_Nullable GetNullable();
int *_Nonnull GetNonnull();
bool cond();

// ==========================================================================
// GAP 1: Constructor argument checking
// Crubit checks CXXConstructExpr args against parameter nullability.
// Nullsafe only checks CallExpr (regular function calls).
// STATUS: NOT IMPLEMENTED
// ==========================================================================

struct TakesNonnull {
    int *_Nonnull ptr_;
    TakesNonnull(int *_Nonnull p) : ptr_(p) {}
};

void test_ctor_nullable_to_nonnull(int *_Nullable p) {
    TakesNonnull t(p); // expected-warning{{passing nullable pointer to nonnull parameter}}
                       // expected-note@-1{{add a null check}}
}

void test_ctor_nonnull_ok(int *_Nonnull p) {
    TakesNonnull t(p); // no warning
}

void test_ctor_narrowed_ok(int *_Nullable p) {
    if (p) {
        TakesNonnull t(p); // no warning — checked
    }
}

void test_ctor_nullptr() {
    TakesNonnull t(nullptr); // expected-warning{{passing nullable pointer to nonnull parameter}}
                             // expected-note@-1{{add a null check}}
}

// ==========================================================================
// GAP 2: Aggregate InitListExpr checking
// S{.field = nullptr} where field is _Nonnull should warn.
// STATUS: NOT IMPLEMENTED
// ==========================================================================

struct AggNonnull {
    int *_Nonnull p;
    int *_Nonnull q;
};

void test_aggregate_init_null() {
    int x;
    AggNonnull a1 = {nullptr, &x}; // expected-warning{{assigning nullable pointer to nonnull member}}
                                   // expected-note@-1{{add a null check}}
}

void test_aggregate_init_nullable(int *_Nullable p) {
    int x;
    AggNonnull a2 = {p, &x}; // expected-warning{{assigning nullable pointer to nonnull member}}
                              // expected-note@-1{{add a null check}}
}

void test_aggregate_init_ok(int *_Nonnull p) {
    int x;
    AggNonnull a3 = {p, &x}; // no warning
}

// ==========================================================================
// GAP 3: Nonnull field nullable at method exit
// A method that nulls a _Nonnull field without restoring it.
// STATUS: NOT IMPLEMENTED
// ==========================================================================

struct OwnerWithNonnullField {
    int *_Nonnull field_;

    OwnerWithNonnullField(int *_Nonnull p) : field_(p) {}

    void steal_field() {
        field_ = nullptr; // expected-warning{{assigning nullable pointer to nonnull member}}
                          // expected-note@-1{{add a null check}}
        // XFAIL-GAP: should ALSO warn "nonnull field is nullable at method exit"
    }

    void swap_ok(int *_Nonnull replacement) {
        int *old = field_;
        field_ = replacement; // no warning — field is still nonnull at exit
    }
};

// ==========================================================================
// GAP 4: Inconsistent annotations across declarations
// Header says _Nonnull, definition says _Nullable.
// STATUS: PARTIALLY WORKING — Clang already warns on conflicting parameter
//         specifiers, but with different wording and only for params.
// ==========================================================================

// Parameter case: Clang already catches this with a different warning.
void inconsistent_func(int *_Nonnull p);
void inconsistent_func(int *_Nullable p) { // expected-warning{{nullability specifier '_Nullable' conflicts with existing specifier '_Nonnull'}}
                                           // expected-note@-2{{previous declaration is here}}
    (void)*p; // expected-warning{{dereference of nullable pointer}}
              // expected-note@-1{{add a null check}}
}

// Return type case: NOT caught.
// Commenting out because Clang errors on the conflicting return type in a
// way that prevents compilation, which is actually stricter than a warning.
// (This gap may already be covered by Clang's type system.)

// ==========================================================================
// GAP 5: Nonnull default argument with null value
// STATUS: NOT IMPLEMENTED
// ==========================================================================

// XFAIL-GAP: these should warn about null/nullable defaults for nonnull params
void default_arg_null(int *_Nonnull p = nullptr);
void default_arg_nullable(int *_Nonnull p = GetNullable());
void default_arg_ok(int *_Nonnull p = GetNonnull()); // no warning

// ==========================================================================
// GAP 6: Nonnull member default initializer
// STATUS: PARTIALLY WORKING — nullptr case already warns, but nullable
//         from function call does not.
// ==========================================================================

struct NonnullMemberDefaultInit {
    int *_Nonnull p = nullptr;        // expected-warning{{initializing nonnull member 'p' with null}}
                                      // expected-note@-1{{remove '_Nonnull' if this member can be null, or remove the null initializer}}
    int *_Nonnull q = GetNullable();  // XFAIL-GAP: should warn "nullable default initializer"
    int *_Nonnull r = GetNonnull();   // no warning
};

// ==========================================================================
// GAP 7: Pointer difference (p - q) — both operands must be nonnull
// STATUS: PARTIAL — warns when nullable is LHS, but not when nullable is RHS
// ==========================================================================

void test_ptr_diff_nullable(int *_Nullable p, int *_Nonnull q) {
    auto d1 = p - q; // expected-warning{{pointer arithmetic on nullable pointer}}
                     // expected-note@-1{{add a null check}}
    auto d2 = q - p; // expected-warning{{pointer arithmetic on nullable pointer}}
                     // expected-note@-1{{add a null check}}
}

void test_ptr_diff_nonnull(int *_Nonnull p, int *_Nonnull q) {
    auto d = p - q; // no warning
}

void test_ptr_diff_after_check(int *_Nullable p, int *_Nonnull q) {
    if (p) {
        auto d = p - q; // no warning — checked
    }
}

// ==========================================================================
// GAP 8: Const method return value caching
// if (obj.get()) { *obj.get(); } should be safe for const methods.
// STATUS: NOT IMPLEMENTED
// ==========================================================================

struct Holder {
    int *_Nullable ptr_;
    int *_Nullable get() const { return ptr_; }
    int *_Nullable get_nonconst() { return ptr_; }
};

void test_const_method_caching(const Holder& h) {
    if (h.get()) {
        // XFAIL-GAP: should NOT warn — same const method, same object
        (void)*h.get(); // expected-warning{{dereference of nullable pointer}}
                        // expected-note@-1{{add a null check}}
    }
}

void test_nonconst_method_warns(Holder& h) {
    if (h.get_nonconst()) {
        // Non-const: second call might return different value. Should warn.
        (void)*h.get_nonconst(); // expected-warning{{dereference of nullable pointer}}
                                 // expected-note@-1{{add a null check}}
    }
}

// ==========================================================================
// GAP 9: Smart pointer release() modeling
// release() returns the pointer and nulls out the smart ptr.
// STATUS: NOT IMPLEMENTED — mock doesn't even have release()
// ==========================================================================

namespace std {
template <typename T>
struct unique_ptr {
    T* ptr;
    using pointer = T*;
    using element_type = T;
    pointer operator->() { return ptr; }
    element_type& operator*() { return *ptr; }
    pointer get() { return ptr; }
    pointer release() { pointer p = ptr; ptr = nullptr; return p; }
    explicit operator bool() const { return ptr != nullptr; }
    void reset() { ptr = nullptr; }
    void reset(T* p) { ptr = p; }
    explicit unique_ptr(T* p) : ptr(p) {}
    unique_ptr() : ptr(nullptr) {}
    unique_ptr(unique_ptr&& other) : ptr(other.ptr) { other.ptr = nullptr; }
    unique_ptr& operator=(unique_ptr&& other) { ptr = other.ptr; other.ptr = nullptr; return *this; }
    unique_ptr(const unique_ptr&) = delete;
    unique_ptr& operator=(const unique_ptr&) = delete;
};

template <typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args);

template <typename T>
T&& move(T& t) noexcept;
} // namespace std

struct Resource { int val; };

void test_release_nulls_smart_ptr() {
    auto sp = std::make_unique<Resource>();
    sp->val = 1; // OK — make_unique is nonnull

    Resource* raw = sp.release();
    raw->val = 2; // XFAIL-GAP: should NOT warn — release returns owned ptr

    sp->val = 3; // XFAIL-GAP: should warn — sp is null after release
}

// ==========================================================================
// GAP 10: Smart pointer get() nullability propagation
// get() returns the underlying pointer with same nullability as smart ptr.
// STATUS: NOT IMPLEMENTED
// ==========================================================================

void test_get_propagates_nullability() {
    auto sp = std::make_unique<Resource>();
    Resource* raw = sp.get();
    raw->val = 1; // XFAIL-GAP: should NOT warn — sp is nonnull

    std::unique_ptr<Resource> empty;
    Resource* raw2 = empty.get();
    raw2->val = 1; // XFAIL-GAP: should warn — empty is null
}

#pragma clang assume_nonnull end
