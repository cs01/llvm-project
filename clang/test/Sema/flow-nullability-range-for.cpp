// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nonnull -fstrict-nullability-inference -std=c++17 %s -verify
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Item { int value; };

template <typename T, int N>
struct Array {
    T data_[N];
    T* begin() { return data_; }
    T* end() { return data_ + N; }
    const T* begin() const { return data_; }
    const T* end() const { return data_ + N; }
};

void test_range_for_no_warn() {
    Array<Item, 3> arr = {};
    for (const auto& item : arr) {
        (void)item.value;
    }
}

void test_range_for_c_array() {
    Item items[4] = {};
    for (const auto& item : items) {
        (void)item.value;
    }
}

void test_deref_still_warns(int* _Nullable p) {
    (void)*p; // expected-warning {{dereferencing nullable pointer}}
}
