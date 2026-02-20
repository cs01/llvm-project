// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -std=c++17 %s -verify
// expected-no-diagnostics

struct Entity {
    int x;
};

#pragma clang assume_nonnull begin

void test_narrowing_before_switch(Entity* _Nullable p, int kind) {
    if (!p) return;
    switch (kind) {
    case 0:
        p->x = 0; // OK - narrowed before switch
        break;
    case 1:
        p->x = 1; // OK - narrowing carries into cases
        break;
    default:
        p->x = -1; // OK
        break;
    }
}

void test_null_check_then_switch(Entity* _Nullable p, int kind) {
    if (p) {
        switch (kind) {
        case 0:
            p->x = 0; // OK
            break;
        case 1:
            p->x = 1; // OK
            break;
        }
    }
}

#pragma clang assume_nonnull end
