// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify

struct Entity {
    int x;
};

[[noreturn]] void fatal(const char* msg);
void log(const char* msg);

#pragma clang assume_nonnull begin

// === if-else where both branches terminate ===

void test_if_else_both_return(Entity* _Nullable p) {
    if (!p) {
        if (true) { return; }
        else { return; }
    }
    p->x = 1; // OK - if always terminates (both branches return)
}

void test_if_else_return_and_noreturn(Entity* _Nullable p) {
    if (!p) {
        if (true) { return; }
        else { fatal("unreachable"); }
    }
    p->x = 1; // OK - if always terminates
}

void test_nested_if_else_terminates(Entity* _Nullable p) {
    if (!p) {
        if (true) {
            if (true) { return; }
            else { return; }
        } else {
            fatal("unreachable");
        }
    }
    p->x = 1; // OK - deeply nested, both paths terminate
}

void test_if_without_else_no_termination(Entity* _Nullable p, bool flag) {
    if (!p) {
        if (flag) { return; }
    }
    p->x = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// === noreturn function in StatementAlwaysTerminates ===

void test_noreturn_function(Entity* _Nullable p) {
    if (!p) {
        fatal("p is null");
    }
    p->x = 1; // OK - noreturn guarantees we don't reach here if p was null
}

void test_noreturn_in_compound(Entity* _Nullable p) {
    if (!p) {
        log("about to die");
        fatal("p is null");
    }
    p->x = 1; // OK
}

// === do-while(0) assertion macro pattern ===

#define MY_ASSERT(cond) do { if (!(cond)) fatal("assertion failed: " #cond); } while(0)

void test_do_while_assert(Entity* _Nullable p) {
    MY_ASSERT(p);
    p->x = 1; // OK - asserted non-null
}

void test_do_while_assert_two_vars(Entity* _Nullable p, Entity* _Nullable q) {
    MY_ASSERT(p);
    MY_ASSERT(q);
    p->x = q->x; // OK - both asserted non-null
}

#pragma clang assume_nonnull end
