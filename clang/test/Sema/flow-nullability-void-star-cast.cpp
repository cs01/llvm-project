// Test: void* casts with nullable-default
// With -fnullability-default=nullable, unannotated void* params are nullable.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Data {
    int value;
};

// void* param is nullable under the default — dereferences warn
void test_void_star_cast_deref(void* obj) {
    Data* p = static_cast<Data*>(obj);
    *p = Data{42}; // expected-warning {{dereferencing nullable pointer}}
    p->value = 1;  // expected-warning {{dereferencing nullable pointer}}
}

// reinterpret_cast to void** — source obj is nullable
void test_reinterpret_cast_void_star_star(void* obj) {
    *reinterpret_cast<void**>(obj) = nullptr; // expected-warning {{dereferencing nullable pointer}}
}

// Explicit _Nullable also warns
void test_nullable_void_star(void* _Nullable obj) {
    Data* p = static_cast<Data*>(obj);
    *p = Data{42}; // expected-warning {{dereferencing nullable pointer}}
}

// Double-pointer casts — source obj is nullable
void test_double_ptr_cast(void* obj) {
    *reinterpret_cast<void**>(obj) = nullptr; // expected-warning {{dereferencing nullable pointer}}
    *reinterpret_cast<int**>(obj) = nullptr;  // expected-warning {{dereferencing nullable pointer}}
}

void test_double_ptr_local(void* obj) {
    void** pp = reinterpret_cast<void**>(obj);
    *pp = nullptr; // expected-warning {{dereferencing nullable pointer}}
}

// With null check, no warning
void test_void_star_checked(void* obj) {
    if (obj) {
        Data* p = static_cast<Data*>(obj);
        *p = Data{42}; // OK — obj was checked
    }
}
