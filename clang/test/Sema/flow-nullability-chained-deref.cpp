// Tests for chained/nested dereference patterns with nullable pointers.
// These patterns are common in real codebases: method chains returning
// nullable, double-dereference through accessor calls, and multi-level
// member narrowing.
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Node {
    int value;
    Node * _Nullable next;
    Node * _Nullable left;
    Node * _Nullable right;
    Node * _Nullable parent;
};

struct Container {
    Node * _Nullable root;
    Node * _Nullable head;
    int size;
};

Node * _Nullable getNode();
Container * _Nullable getContainer();
int getInt();

#pragma clang assume_nonnull begin

// === Direct chained dereference: getNode()->value ===
// Calling a function that returns nullable, then immediately accessing a member.

void test_direct_chain_warns() {
    int v = getNode()->value; // expected-warning{{dereferencing nullable pointer}}
    (void)v;
}

void test_direct_chain_guarded() {
    Node * _Nullable n = getNode();
    if (n) {
        int v = n->value; // OK
        (void)v;
    }
}

// === Double chain: getContainer()->root->value ===

void test_double_chain_warns() {
    (void)getContainer()->root; // expected-warning{{dereferencing nullable pointer}}
}

void test_double_chain_partial_guard() {
    Container * _Nullable c = getContainer();
    if (c) {
        c->root->value = 1; // expected-warning{{dereferencing nullable pointer}}
    }
}

void test_double_chain_full_guard() {
    Container * _Nullable c = getContainer();
    if (c && c->root) {
        c->root->value = 1; // OK — both narrowed
    }
}

// === Triple chain through linked list ===

void test_triple_chain(Node * _Nullable head) {
    if (head && head->next && head->next->next) {
        head->next->next->value = 42; // OK — all three narrowed
    }
}

// === Known limitation: multi-level member narrowing ===
// The analysis tracks (VarDecl, FieldDecl) pairs for member narrowing.
// When head->next is narrowed, accessing head->next->next is a dereference
// of the narrowed member. The analysis currently does not re-check that
// the result (head->next->next) is also nullable. This is a false negative
// accepted for the current intraprocedural design.

void test_triple_chain_partial(Node * _Nullable head) {
    if (head && head->next) {
        // head->next is narrowed, but head->next->next is still nullable.
        // The analysis currently does not warn here (accepted false negative).
        head->next->next->value = 42; // no warning (known limitation)
    }
}

// === Method return chaining ===

struct Builder {
    Node * _Nullable node;

    Builder * _Nullable setNode(Node * _Nonnull n) {
        node = n;
        return this;
    }

    Node * _Nullable getResult() {
        return node;
    }
};

Builder * _Nullable getBuilder();

void test_builder_chain_warns() {
    getBuilder()->getResult(); // expected-warning{{dereferencing nullable pointer}}
}

void test_builder_chain_guarded() {
    Builder * _Nullable b = getBuilder();
    if (b) {
        Node * _Nullable result = b->getResult();
        if (result) {
            (void)result->value; // OK — both guarded
        }
    }
}

// === Pointer-to-pointer (T**) ===
// Known limitation: the analysis tracks narrowing for VarDecls and
// (VarDecl, FieldDecl) pairs. It does NOT track *pp as a narrowable
// expression, so even after checking *pp, dereferences through *pp
// still warn.

void test_ptr_to_ptr(Node * _Nullable * _Nullable pp) {
    if (pp && *pp) {
        // *pp was checked but the analysis can't track it
        (*pp)->value = 1; // expected-warning{{dereferencing nullable pointer}}
    }
}

void test_ptr_to_ptr_via_local(Node * _Nullable * _Nullable pp) {
    if (!pp) return;
    Node * _Nullable p = *pp; // capture into a local variable
    if (p) {
        p->value = 1; // OK — local variable is tracked
    }
}

// === Array of nullable pointers ===

void test_array_of_nullable(Node * _Nullable nodes[], int n) {
    for (int i = 0; i < n; i++) {
        Node * _Nullable cur = nodes[i];
        if (cur) {
            cur->value = i; // OK — narrowed via local
        }
    }
}

// === Conditional chain: p ? p->next : nullptr, then use ===

void test_conditional_chain(Node * _Nullable p) {
    Node * _Nullable next = p ? p->next : nullptr;
    if (next) {
        next->value = 1; // OK — narrowed
    }
}

// === Assignment from chain ===

void test_assign_from_chain() {
    Node * _Nullable n = getNode();
    if (!n) return;

    // n is narrowed, but n->next is still nullable
    Node * _Nullable child = n->next;
    if (child) {
        child->value = 1; // OK
    }
}

// === Chained access in loop ===

void test_chain_in_loop() {
    Node * _Nullable head = getNode();
    for (Node * _Nullable p = head; p; p = p->next) {
        // p is narrowed by loop condition
        if (p->left && p->left->right) {
            p->left->right->value = 0; // OK — all narrowed
        }
    }
}

// === Chained return value ===

Node * _Nullable get_grandchild(Node * _Nullable n) {
    if (n && n->next) {
        return n->next->next; // OK — n->next narrowed; returns nullable
    }
    return nullptr;
}

// === Container accessor pattern ===

void test_container_accessor() {
    Container * _Nullable c = getContainer();
    if (!c) return;

    // c->root is nullable, need to check
    if (!c->root) return;
    c->root->value = 1; // OK — both narrowed

    // But root->next needs separate check
    if (c->root->next) {
        c->root->next->value = 2; // OK
    }
}

// === Cascade of null-checked returns ===

Node * _Nullable safe_next(Node * _Nullable n) {
    if (!n) return nullptr;
    return n->next; // OK — n narrowed
}

void test_cascade() {
    Node * _Nullable n = getNode();
    Node * _Nullable child = safe_next(n);
    // child is nullable (return type says so), need to check
    if (child) {
        child->value = 1; // OK
    }
}

// === Nested struct with multiple nullable fields ===

struct Tree {
    int data;
    Tree * _Nullable left;
    Tree * _Nullable right;
    Tree * _Nullable parent;
};

void test_tree_traversal(Tree * _Nullable root) {
    if (!root) return;

    // Check left subtree
    if (root->left) {
        root->left->data = 1; // OK
        if (root->left->left) {
            root->left->left->data = 2; // OK — deeply narrowed
        }
    }

    // Check right subtree — independent narrowing
    if (root->right && root->right->parent) {
        root->right->parent->data = 3; // OK
    }
}

// === Chained dereference with reassignment invalidation ===

void test_chain_invalidation(Node * _Nullable p) {
    if (p && p->next) {
        p->next->value = 1; // OK — both narrowed
        p = getNode();       // reassign p — narrowing gone
        // p is now nullable again
        if (p) {
            p->value = 2; // OK — re-narrowed
        }
    }
}

#pragma clang assume_nonnull end
