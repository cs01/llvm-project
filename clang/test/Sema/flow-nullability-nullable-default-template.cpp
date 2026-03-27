// Test: Does the checker catch dereferences of explicit _Nullable return types?
// This mimics the getComponent<T>() pattern from Clay ECS.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Component {
    int value;
    void setValue(int v) { value = v; }
};

struct Entity {
    // Template method returning T* _Nullable
    template<typename T>
    T* _Nullable getComponent() { return nullptr; }

    // Non-template returning pointer — explicitly nullable
    Component* _Nullable getFirstComponent() { return nullptr; }
};

// Case 1: Non-template function → local var → arrow deref
void test_non_template(Entity* e) {
    Component* c = e->getFirstComponent(); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    c->setValue(42);     // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Case 2: Template function → local var → arrow deref
void test_template(Entity* e) {
    Component* c = e->getComponent<Component>(); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    c->setValue(42);     // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Case 3: Chained call → local var → data member access
void test_data_member(Entity* e) {
    Component* c = e->getComponent<Component>(); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    c->value = 1;        // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Case 4: With null check — should NOT warn for c, still warns for e
void test_with_check(Entity* e) {
    Component* c = e->getComponent<Component>(); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    if (c != nullptr) {
        c->setValue(42); // OK — c is narrowed
    }
}
