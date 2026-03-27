// Comprehensive C test for flow-sensitive nullability analysis.
// Covers C-specific patterns: nested structs, restrict, compound literals,
// flexible array members, and C99/C11 features.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c11 %s -verify

struct Point {
    int x;
    int y;
};

struct Line {
    struct Point * _Nullable start;
    struct Point * _Nullable end;
};

struct Node {
    int value;
    struct Node * _Nullable next;
    struct Node * _Nullable prev;
};

struct Point * _Nullable getPoint(void);
struct Node * _Nullable getNode(void);
int getInt(void);

// === Nested struct pointer access ===

void test_nested_struct(struct Line * _Nullable line) {
    if (line && line->start) {
        line->start->x = 1; // OK — both narrowed
    }
}

void test_nested_not_checked(struct Line * _Nonnull line) {
    line->start->x = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Double-linked list traversal ===

void test_doubly_linked(struct Node * _Nullable head) {
    for (struct Node * _Nullable p = head; p; p = p->next) {
        p->value = 0; // OK — narrowed by loop condition
        if (p->prev) {
            p->prev->value = -1; // OK — narrowed
        }
    }
}

// === Reverse traversal ===

void test_reverse_traversal(struct Node * _Nullable tail) {
    struct Node * _Nullable p = tail;
    while (p) {
        p->value = 0; // OK
        p = p->prev;
    }
}

// === restrict pointer ===

void test_restrict(int * restrict p) {
    *p = 42; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_restrict_checked(int * restrict _Nullable p) {
    if (p)
        *p = 42; // OK
}

// === Compound literal ===

void test_compound_literal(void) {
    int *p = &(int){42};
    *p = 0; // OK — address-of compound literal is nonnull
}

// === Designated initializer (struct members are stack-allocated) ===

void test_designated_init(void) {
    struct Point pt = {.x = 1, .y = 2};
    struct Point *pp = &pt;
    pp->x = 3; // OK — address-of
}

// === Array of pointers ===

void test_pointer_array(struct Node * _Nullable * _Nonnull nodes, int n) {
    for (int i = 0; i < n; i++) {
        struct Node * _Nullable node = nodes[i];
        if (node) {
            node->value = i; // OK — narrowed via local variable
        }
    }
}

// === Multiple sequential checks ===

void test_sequential_checks(struct Node * _Nullable a,
                            struct Node * _Nullable b,
                            struct Node * _Nullable c) {
    if (!a) return;
    if (!b) return;
    if (!c) return;
    a->value = b->value + c->value; // OK — all narrowed
}

// === Null check with comparison operators ===

void test_comparison_styles(struct Node *p) {
    // All these are equivalent null checks
    if (p != 0) {
        p->value = 1; // OK
    }
}

void test_comparison_null_macro(struct Node *p) {
    if (p != ((void*)0)) {
        p->value = 1; // OK
    }
}

// === Function returning _Nonnull ===

struct Node * _Nonnull createNode(void);

void test_nonnull_return(void) {
    struct Node *n = createNode();
    n->value = 1; // OK — _Nonnull return
}

// === Void pointer cast patterns ===

void test_void_ptr_cast(void * _Nonnull raw) {
    struct Node *n = (struct Node *)raw;
    // void* is _Nonnull, so cast result is nonnull
    n->value = 1; // OK — nonnull source
}

void test_void_ptr_nullable(void * _Nullable raw) {
    struct Node *n = (struct Node *)raw;
    n->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === Conditional operator ===

void test_cond_op(struct Node * _Nullable p, struct Node * _Nullable q) {
    struct Node *r = p ? p : q;
    // r might be q which is nullable
    if (r)
        r->value = 1; // OK — narrowed
}

// === Nested conditionals ===

void test_nested_cond(struct Node * _Nullable p) {
    if (p) {
        if (p->next) {
            if (p->next->next) {
                p->next->next->value = 0; // OK — all narrowed
            }
        }
    }
}

// === Goto-based cleanup pattern (common in C) ===

int test_goto_cleanup(struct Node * _Nullable p) {
    int result = -1;
    if (!p) goto out;
    result = p->value; // OK — narrowed
out:
    return result;
}

// === Switch with null check in cases ===

void test_switch_null_check(struct Node * _Nullable p, int choice) {
    switch (choice) {
    case 0:
        if (p)
            p->value = 0; // OK
        break;
    case 1:
        if (!p) return;
        p->value = 1; // OK
        break;
    default:
        break;
    }
}

// === Comma operator ===

void test_comma(struct Node * _Nullable p) {
    if (!p) return;
    (void)p->value; // OK — narrowed (comma test simplified)
}

// === sizeof does not evaluate ===

void test_sizeof_unevaluated(struct Node * _Nullable p) {
    int s = sizeof(p->value); // OK — sizeof is unevaluated
    (void)s;
}

// === Pointer subtraction ===

void test_ptr_subtraction(int *a, int *b) {
    long diff = a - b; // OK — subtraction, not dereference
    (void)diff;
}
