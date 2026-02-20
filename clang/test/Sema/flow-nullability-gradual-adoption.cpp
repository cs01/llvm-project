// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=unspecified -std=c++17 %s -verify

struct Entity {
    int x;
    int value() const { return x; }
};

Entity* _Nullable getHead();
Entity* _Nullable getChest();
Entity* getUnannotated();

// === OUTSIDE pragma: no warnings even with _Nullable ===
// This is how folly/Clay headers behave — they have _Nullable
// annotations but are not inside an assume_nonnull region.

void test_outside_pragma_no_warn(Entity* _Nullable p) {
    p->x = 1;              // OK - outside pragma, analysis not active
    (*p).x = 1;            // OK - outside pragma, analysis not active
    getHead()->x = 1;      // OK - outside pragma, analysis not active
}

// === INSIDE pragma: warnings on explicit _Nullable ===

#pragma clang assume_nonnull begin

void test_explicit_nullable_param_arrow_warns(Entity* _Nullable p) {
    p->x = 1;              // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
}

void test_explicit_nullable_param_arrow_with_check(Entity* _Nullable p) {
    if (!p) return;
    p->x = 1;              // OK - narrowed to nonnull
}

void test_explicit_nullable_param_star_warns(Entity* _Nullable p) {
    (*p).x = 1;            // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
}

void test_explicit_nullable_param_star_with_check(Entity* _Nullable p) {
    if (!p) return;
    (*p).x = 1;            // OK - narrowed to nonnull
}

void test_chained_nullable_arrow_warns() {
    getHead()->x = 1;      // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
}

void test_chained_nullable_arrow_method_warns() {
    int v = getHead()->value(); // expected-warning{{dereferencing nullable pointer of type 'Entity * _Nullable'}}
}

void test_unannotated_no_warn() {
    Entity* e = getUnannotated();
    e->x = 1;              // OK - unannotated pointer, unspecified mode
    (*e).x = 1;            // OK - unannotated pointer, unspecified mode
}

void test_unannotated_param_no_warn(Entity* p) {
    p->x = 1;              // OK - unannotated param, unspecified mode
}

#pragma clang assume_nonnull end
