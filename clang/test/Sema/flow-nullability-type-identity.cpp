// Verify that -fflow-sensitive-nullability does NOT affect type identity.
// Nullability qualifiers are type sugar in Clang — they don't participate
// in template argument deduction, std::is_same, decltype, or overload
// resolution. This must remain true even when the flag infers
// _Null_unspecified on unannotated pointers.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

template<typename T, typename U>
struct is_same { static constexpr bool value = false; };
template<typename T>
struct is_same<T, T> { static constexpr bool value = true; };

// Force a diagnostic that prints the type name — used to verify
// types print as "int *", not "int * _Null_unspecified".
template<typename T> struct show_type;

#pragma clang assume_nonnull begin

// --- decltype preserves bare pointer type ---

void test_decltype_local() {
    int x = 0;
    int *p = &x;
    static_assert(is_same<decltype(p), int*>::value, "");
}

void test_decltype_param(int *p) {
    static_assert(is_same<decltype(p), int*>::value, "");
}

// --- auto deduction ---

void test_auto_deduction() {
    int x = 0;
    auto p = &x;
    static_assert(is_same<decltype(p), int*>::value, "");
}

// --- template argument deduction ---

template<typename T>
void accept(T) {
    static_assert(is_same<T, int*>::value, "");
}

void test_template_deduction() {
    int x = 0;
    int *p = &x;
    accept(p);
}

// --- explicit template argument matching ---

template<typename T> void accept_ptr(T*) {}

void test_explicit_template_arg() {
    int x = 0;
    int *p = &x;
    accept_ptr<int>(p); // must match int*, not int* _Null_unspecified
}

// --- Nullability qualifiers are sugar: all compare equal ---

void test_nullability_is_sugar() {
    int x;
    int *bare = &x;
    int * _Nullable nullable = &x;
    int * _Nonnull nonnull = &x;
    int * _Null_unspecified unspec = &x;

    // All four are the same type for is_same purposes
    static_assert(is_same<decltype(bare), decltype(nullable)>::value, "");
    static_assert(is_same<decltype(bare), decltype(nonnull)>::value, "");
    static_assert(is_same<decltype(bare), decltype(unspec)>::value, "");
}

// --- Function return type deduction ---

auto make_ptr() {
    int *p = new int(42);
    return p;
}

void test_return_type_deduction() {
    static_assert(is_same<decltype(make_ptr()), int*>::value, "");
}

// --- Const pointer ---

void test_const_ptr() {
    const int x = 0;
    const int *p = &x;
    static_assert(is_same<decltype(p), const int*>::value, "");
}

// --- Pointer to pointer ---

void test_ptr_to_ptr() {
    int x;
    int *p = &x;
    int **pp = &p;
    static_assert(is_same<decltype(pp), int**>::value, "");
}

#pragma clang assume_nonnull end
