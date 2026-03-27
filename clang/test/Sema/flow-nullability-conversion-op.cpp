// Tests that conversion operators (operator T*()) don't trigger spurious
// nullability-inference warnings. Without the IK_ConversionFunctionId
// exclusion in SemaType.cpp, the compiler would try to infer nullability
// on the return type of operator void*(), causing errors.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nonnull -std=c++17 %s -verify
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

typedef void* bool_type;

struct ConvertToRawPtr {
    void* data;
    operator void*() const { return data; }
};

struct ConvertToTypedef {
    bool_type data;
    operator bool_type() const { return data; }
};

struct ConvertToNonPointer {
    int value;
    operator int() const { return value; }
};

void test_conversions() {
    ConvertToRawPtr a;
    void* p = a;

    ConvertToTypedef b;
    bool_type q = b;

    ConvertToNonPointer c;
    int n = c;
}

void test_deref_still_warns(int* _Nullable p) {
    (void)*p; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
