// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node* next;
};

template <typename T>
struct SmartPtr {
    T* ptr;
    T* operator->() { return ptr; }
    T& operator*() { return *ptr; } // expected-warning {{dereferencing nullable pointer}}
    T* get() { return ptr; }
};

#pragma clang assume_nonnull begin

void test_smart_ptr_arrow(SmartPtr<Node> sp) {
    sp->value = 1; // OK - operator-> return should not warn at call site
    sp->next = nullptr; // OK
}

void test_smart_ptr_deref(SmartPtr<Node> sp) {
    (*sp).value = 1; // OK - operator* returns T&, no pointer deref at call site  // expected-note {{in instantiation of member function 'SmartPtr<Node>::operator*' requested here}}
}

void test_smart_ptr_get_arrow(SmartPtr<Node> sp) {
    sp.get()->value = 1; // expected-warning {{dereferencing nullable pointer}}
}

void test_raw_ptr_still_warns(Node* _Nullable p) {
    p->value = 1; // expected-warning {{dereferencing nullable pointer}}
}

void test_raw_ptr_narrowed(Node* _Nullable p) {
    if (p) {
        p->value = 1; // OK - narrowed
    }
}

template <typename T>
struct UniquePtr {
    T* ptr;
    using pointer = T*;
    using element_type = T;
    pointer operator->() { return ptr; }
    element_type& operator*() { return *ptr; }
    pointer get() { return ptr; }
};

void test_unique_ptr_arrow(UniquePtr<Node> up) {
    up->value = 1; // OK - operator-> return should not warn at call site
}

void test_unique_ptr_get(UniquePtr<Node> up) {
    up.get()->value = 1; // expected-warning {{dereferencing nullable pointer}}
}

struct Container {
    struct Iterator {
        Node* ptr;
        Node* operator->() { return ptr; }
        Node& operator*() { return *ptr; } // OK - inside assume_nonnull, ptr is _Nonnull
    };

    Iterator begin();
    Iterator end();
};

void test_iterator_arrow(Container c) {
    auto it = c.begin();
    it->value = 1; // OK - operator-> on iterator should not warn at call site
}

#pragma clang assume_nonnull end
