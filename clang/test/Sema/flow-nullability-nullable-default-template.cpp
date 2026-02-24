// Test: Does nullable-default catch dereferences through template return types?
// This mimics the getComponent<T>() pattern from Clay ECS.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Component {
    int value;
    void setValue(int v) { value = v; }
};

struct Entity {
    // Template method returning T* — nullable by default
    template<typename T>
    T* getComponent() { return nullptr; }

    // Non-template returning pointer — nullable by default
    Component* getFirstComponent() { return nullptr; }
};

// Case 1: Non-template function → local var → arrow deref
void test_non_template(Entity* e) {  // e is nullable by default
    Component* c = e->getFirstComponent();  // expected-warning{{dereferencing nullable pointer}}
    c->setValue(42);     // expected-warning{{dereferencing nullable pointer}}
}

// Case 2: Template function → local var → arrow deref
void test_template(Entity* e) {
    Component* c = e->getComponent<Component>();  // expected-warning{{dereferencing nullable pointer}}
    c->setValue(42);     // expected-warning{{dereferencing nullable pointer}}
}

// Case 3: Chained call → local var → data member access
void test_data_member(Entity* e) {
    Component* c = e->getComponent<Component>();  // expected-warning{{dereferencing nullable pointer}}
    c->value = 1;        // expected-warning{{dereferencing nullable pointer}}
}

// Case 4: With null check — should NOT warn for c
void test_with_check(Entity* e) {
    if (!e) return;
    Component* c = e->getComponent<Component>();  // OK — e is narrowed
    if (c != nullptr) {
        c->setValue(42); // OK — c is narrowed
    }
}
