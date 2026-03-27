// Tests for smart pointer null dereference detection in flow-sensitive nullability.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node* next;
};

// Minimal std smart pointer mocks — must be in namespace std for detection.
namespace std {

template <typename T>
struct unique_ptr {
    T* ptr;
    using pointer = T*;
    using element_type = T;
    pointer operator->() { return ptr; }
    element_type& operator*() { return *ptr; }
    pointer get() { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    void reset() { ptr = nullptr; }
    void reset(T* p) { ptr = p; }
    // Move constructor / assignment omitted — just enough for AST structure
};

template <typename T>
struct shared_ptr {
    T* ptr;
    T* operator->() { return ptr; }
    T& operator*() { return *ptr; }
    T* get() { return ptr; }
    explicit operator bool() const { return ptr != nullptr; }
    void reset() { ptr = nullptr; }
    void reset(T* p) { ptr = p; }
};

template <typename T, typename... Args>
unique_ptr<T> make_unique(Args&&... args);

template <typename T, typename... Args>
shared_ptr<T> make_shared(Args&&... args);

template <typename T>
T&& move(T& t) noexcept;

} // namespace std

#pragma clang assume_nonnull begin

// Non-std smart pointer (should NOT trigger smart pointer warnings)
template <typename T>
struct CustomPtr {
    T* ptr;
    T* operator->() { return ptr; }
    T& operator*() { return *ptr; }
};

// Iterator (should NOT trigger smart pointer warnings)
struct Container {
    struct Iterator {
        Node* ptr;
        Node* operator->() { return ptr; }
        Node& operator*() { return *ptr; }
    };
    Iterator begin();
    Iterator end();
};

// --- Basic dereference warnings ---

void test_sp_deref_warns(std::unique_ptr<Node> sp) {
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void test_shared_ptr_deref_warns(std::shared_ptr<Node> sp) {
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// --- Narrowing via null check ---

void test_sp_narrowed_by_check(std::unique_ptr<Node> sp) {
    if (sp) {
        sp->value = 1; // OK — narrowed by bool check
    }
}

void test_sp_narrowed_negated(std::unique_ptr<Node> sp) {
    if (!sp)
        return;
    sp->value = 1; // OK — narrowed by early return
}

// --- make_unique/make_shared narrow ---

void test_make_unique_narrows() {
    auto sp = std::make_unique<Node>();
    sp->value = 1; // OK — make_unique always returns non-null
}

void test_make_shared_narrows() {
    auto sp = std::make_shared<Node>();
    sp->value = 1; // OK — make_shared always returns non-null
}

// --- reset() makes nullable ---

void test_reset_makes_nullable(std::unique_ptr<Node> sp) {
    if (sp) {
        sp->value = 1; // OK
    }
    sp.reset();
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void test_reset_with_arg_narrows(std::unique_ptr<Node> sp) {
    sp.reset(new Node());
    sp->value = 1; // OK — reset(ptr) gives it a value
}

void test_reset_nullptr_stays_nullable(std::unique_ptr<Node> sp) {
    if (sp) {
        sp->value = 1; // OK — narrowed
    }
    sp.reset(nullptr);
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// --- std::move makes source nullable ---

void test_move_makes_source_nullable(std::unique_ptr<Node> sp) {
    if (sp) {
        sp->value = 1; // OK
    }
    auto other = std::move(sp);
    sp->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// --- Member smart pointers ---
// Member smart pointers do NOT warn by default (too many false positives on
// members initialized in constructors). They only warn when the current
// function has evidence of nullability (reset, move, etc).

struct Owner {
    std::unique_ptr<Node> csm_;

    // No warning — no evidence of nullability within this function.
    // Most member unique_ptrs are set in the constructor and always valid.
    void use_no_evidence() {
        csm_->value = 1; // OK — no evidence of nullability
    }

    // Warning — reset() without args makes it nullable in this function
    void use_after_reset() {
        csm_->value = 1; // OK — before reset
        csm_.reset();
        csm_->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    }

    // OK — reset with arg re-narrows it
    void use_after_reset_with_arg() {
        csm_.reset(new Node());
        csm_->value = 1; // OK — reset(ptr) narrows
    }

    // Warning — std::move makes it nullable
    void use_after_move() {
        auto other = std::move(csm_);
        csm_->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    }

    // OK — narrowed by null check after reset
    void use_safe_after_reset() {
        csm_.reset();
        if (csm_) {
            csm_->value = 1; // OK — narrowed
        }
    }
};


// --- Non-std smart pointers should NOT warn (kept as before) ---

void test_custom_ptr_no_warn(CustomPtr<Node> cp) {
    cp->value = 1; // OK — not a std smart pointer, skip operator->
}

void test_iterator_no_warn(Container c) {
    auto it = c.begin();
    it->value = 1; // OK — iterator, not a smart pointer
}

// --- .get() returns an unannotated raw pointer, no warning ---
// (The smart pointer deref check via operator-> is the preferred warning path.)

void test_get_no_warning_unannotated(std::unique_ptr<Node> sp) {
    sp.get()->value = 1; // OK — get() return type is unannotated
}

// --- Raw pointers still work as before ---

void test_raw_ptr_still_warns(Node* _Nullable p) {
    p->value = 1; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void test_raw_ptr_narrowed(Node* _Nullable p) {
    if (p) {
        p->value = 1; // OK — narrowed
    }
}

#pragma clang assume_nonnull end
