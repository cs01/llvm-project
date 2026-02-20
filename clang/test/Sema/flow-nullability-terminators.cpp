// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 -fcxx-exceptions %s -verify
// expected-no-diagnostics

struct Entity {
    int x;
};

[[noreturn]] void fatal(const char* msg);

#pragma clang assume_nonnull begin

// === throw as terminator ===

void test_throw_narrows(Entity* _Nullable p) {
    if (!p) throw "null pointer";
    p->x = 1; // OK - throw terminates
}

void test_throw_in_compound(Entity* _Nullable p) {
    if (!p) {
        throw "null";
    }
    p->x = 1; // OK
}

// === goto as terminator ===

void test_goto_narrows(Entity* _Nullable p) {
    if (!p) goto cleanup;
    p->x = 1; // OK - goto terminates
cleanup:
    return;
}

// === break as terminator (in loop) ===

void test_break_narrows(Entity* _Nullable p) {
    for (int i = 0; i < 10; i++) {
        if (!p) break;
        p->x = i; // OK - break terminates
    }
}

void test_break_while(Entity* _Nullable p) {
    while (true) {
        if (!p) break;
        p->x = 1; // OK
    }
}

// === continue as terminator (in loop) ===

void test_continue_narrows(Entity* _Nullable p) {
    for (int i = 0; i < 10; i++) {
        if (!p) continue;
        p->x = i; // OK - continue terminates
    }
}

// === return in else (positive check) ===

// CFG correctly models that when the else-branch returns, post-if code
// is only reachable from the then-branch where p was narrowed.
void test_positive_check_else_return(Entity* _Nullable p) {
    if (p) {
        // use p
    } else {
        return;
    }
    p->x = 1; // OK - only reachable when p is non-null
}

// === combinations ===

void test_noreturn_then_deref(Entity* _Nullable p) {
    if (!p) fatal("null");
    p->x = 1; // OK
}

void test_two_checks_return(Entity* _Nullable p, Entity* _Nullable q) {
    if (!p) return;
    if (!q) return;
    p->x = q->x; // OK - both narrowed
}

#pragma clang assume_nonnull end
