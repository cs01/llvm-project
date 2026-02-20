// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node* next;
};

[[noreturn]] void abort_handler(const char* msg);

#define INVARIANT(cond) \
  { \
    if (!(cond)) { \
      abort_handler("invariant failed"); \
    } \
  }

#define INVARIANT_MSG(cond, msg) \
  { \
    if (!(cond)) { \
      abort_handler(msg); \
    } \
  }

#pragma clang assume_nonnull begin

void test_basic_brace_assert(Node* _Nullable p) {
    INVARIANT(p);
    p->value = 1; // OK - INVARIANT ensures p is non-null
}

void test_brace_assert_with_message(Node* _Nullable p) {
    INVARIANT_MSG(p, "p must not be null");
    p->value = 1; // OK
}

void test_brace_assert_ne_nullptr(Node* _Nullable p) {
    INVARIANT(p != nullptr);
    p->value = 1; // OK - p != nullptr checked
}

void test_brace_assert_multiple_vars(Node* _Nullable p, Node* _Nullable q) {
    INVARIANT(p);
    INVARIANT(q);
    p->value = q->value; // OK - both narrowed
}

void test_brace_assert_member(Node* _Nullable p) {
    INVARIANT(p);
    INVARIANT(p->next);
    p->next->value = 1; // OK - both p and p->next narrowed through macro
}

void test_no_assert_still_warns(Node* _Nullable p) {
    p->value = 1; // expected-warning {{dereferencing nullable pointer}}
}

void test_manual_bare_brace_noreturn(Node* _Nullable p) {
    {
        if (!p) {
            abort_handler("null");
        }
    }
    p->value = 1; // OK - bare braces with noreturn narrow outward
}

void test_nested_brace_assert(Node* _Nullable p, Node* _Nullable q) {
    {
        if (!p) { abort_handler("p"); }
        if (!q) { abort_handler("q"); }
    }
    p->value = q->value; // OK - both narrowed
}

void test_brace_assert_does_not_affect_unrelated(Node* _Nullable p, Node* _Nullable q) {
    INVARIANT(p);
    q->value = 1; // expected-warning {{dereferencing nullable pointer}}
}

struct Widget {
    Node* _Nullable data;
    int x;

    void test_this_arrow() {
        this->x = 1; // OK - 'this' is never null
    }

    int test_this_deref() {
        return (*this).x; // OK - 'this' is never null
    }

    void test_this_member_narrowing() {
        INVARIANT(data);
        data->value = 1; // OK - data narrowed by INVARIANT
    }

    void test_this_member_if_narrowing() {
        if (data) {
            data->value = 1; // OK - data narrowed by if
        }
    }

    void test_this_member_no_narrowing() {
        data->value = 1; // expected-warning {{dereferencing nullable pointer}}
    }
};

void test_and_member_narrowing(Node* _Nullable p) {
    if (p && p->next) {
        p->next->value = 1; // OK - both p and p->next narrowed by && condition
    }
}

void test_and_member_no_narrowing(Node* _Nullable p) {
    if (p) {
        p->next->value = 1; // expected-warning {{dereferencing nullable pointer}}
    }
}

void test_or_member_early_return(Node* _Nullable p) {
    if (!p || !p->next) return;
    p->next->value = 1; // OK - both p and p->next narrowed by early return
}

#pragma clang assume_nonnull end
