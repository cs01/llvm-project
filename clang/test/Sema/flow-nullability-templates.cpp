// Tests for template interactions with flow-sensitive nullability analysis.
// Templates are a major source of false positives in type-based analyses —
// template instantiation can bake _Nullable into cast result types even when
// the source is unannotated.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node * _Nullable next;
};

Node * _Nullable getNode();
Node * _Nonnull getSafeNode();

#pragma clang assume_nonnull begin

// === Template functions with pointer parameters ===

template <typename T>
void deref_unchecked(T * _Nullable p) {
    (void)p->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

template <typename T>
void deref_guarded(T * _Nullable p) {
    if (p)
        (void)p->value; // OK — narrowed
}

template <typename T>
void deref_nonnull(T * _Nonnull p) {
    (void)p->value; // OK — _Nonnull
}

void test_template_functions() {
    Node * _Nullable n = getNode();
    deref_unchecked(n); // expected-note{{in instantiation of function template specialization 'deref_unchecked<Node>' requested here}}
    deref_guarded(n);
    deref_nonnull(getSafeNode());
}

// === Template class with nullable member ===

template <typename T>
struct Wrapper {
    T * _Nullable ptr;

    void use_unchecked() {
        (void)ptr->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    }

    void use_guarded() {
        if (ptr)
            (void)ptr->value; // OK
    }
};

void test_template_class() {
    Wrapper<Node> w;
    w.use_unchecked(); // expected-note{{in instantiation of member function 'Wrapper<Node>::use_unchecked' requested here}}
    w.use_guarded();
}

// === Template with multiple pointer params of different nullability ===

template <typename T>
void mixed_nullability(T * _Nonnull safe, T * _Nullable risky) {
    (void)safe->value; // OK — _Nonnull
    (void)risky->value; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_mixed() {
    mixed_nullability(getSafeNode(), getNode()); // expected-note{{in instantiation of function template specialization 'mixed_nullability<Node>' requested here}}
}

// === Template that narrows then uses ===

template <typename T>
T* _Nullable find(T * _Nullable head, int target) {
    for (T * _Nullable p = head; p; p = p->next) {
        if (p->value == target) // OK — narrowed by loop condition
            return p;
    }
    return nullptr;
}

void test_find() {
    Node * _Nullable head = getNode();
    find(head, 42);
}

// === Template with cast — the key false-positive scenario ===
// Template instantiation can produce casts with _Nullable in the dest type.
// The analysis should look through these casts to the source type.

template <typename T>
T* cast_and_use(void *raw) {
    T *p = static_cast<T *>(raw);
    // raw is void* (unannotated in nullable-default mode), but static_cast
    // may bake the template param's nullability into the result.
    // Should not warn — source (raw) is not explicitly _Nullable.
    (void)p->value; // OK — unannotated source through cast
    return p;
}

void test_template_cast() {
    int dummy;
    cast_and_use<Node>(&dummy);
}

// === Non-type template parameters (no effect on nullability) ===

template <int N>
void fixed_iteration(Node * _Nullable p) {
    if (!p) return;
    for (int i = 0; i < N; i++)
        (void)p->value; // OK — narrowed
}

void test_non_type_template() {
    fixed_iteration<10>(getNode());
}

// === Template with auto return type ===

template <typename T>
auto safe_access(T * _Nullable p, int fallback) {
    if (p)
        return p->value; // OK
    return fallback;
}

void test_auto_return() {
    safe_access(getNode(), -1);
}

// === Dependent type that resolves to pointer ===

template <typename T>
struct PointerHolder {
    using Ptr = T*;
    Ptr _Nullable held;

    void use() {
        if (held)
            (void)held->value; // OK — narrowed
    }
};

void test_dependent_type() {
    PointerHolder<Node> h;
    h.use();
}

#pragma clang assume_nonnull end
