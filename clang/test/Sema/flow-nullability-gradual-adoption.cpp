// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=unspecified -std=c++17 %s -verify

struct Entity {
    int x;
    int value() const { return x; }
};

Entity* _Nullable getHead();
Entity* _Nullable getChest();
Entity* getUnannotated();

// === OUTSIDE pragma: warnings on explicit _Nullable ===
// Functions with any nullability annotation activate the analysis,
// even without a pragma or -fnullability-default flag.

void test_outside_pragma_explicit_nullable(Entity* _Nullable p) {
    p->x = 1;              // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    (*p).x = 1;            // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
    getHead()->x = 1;      // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// Unannotated functions outside pragma still don't activate
void test_outside_pragma_unannotated(Entity* p) {
    p->x = 1;              // OK - no annotations, analysis not active
}

// === INSIDE pragma: warnings on explicit _Nullable ===

#pragma clang assume_nonnull begin

void test_explicit_nullable_param_arrow_warns(Entity* _Nullable p) {
    p->x = 1;              // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_explicit_nullable_param_arrow_with_check(Entity* _Nullable p) {
    if (!p) return;
    p->x = 1;              // OK - narrowed to nonnull
}

void test_explicit_nullable_param_star_warns(Entity* _Nullable p) {
    (*p).x = 1;            // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_explicit_nullable_param_star_with_check(Entity* _Nullable p) {
    if (!p) return;
    (*p).x = 1;            // OK - narrowed to nonnull
}

void test_chained_nullable_arrow_warns() {
    getHead()->x = 1;      // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_chained_nullable_arrow_method_warns() {
    int v = getHead()->value(); // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_unannotated_no_warn() {
    Entity* e = getUnannotated();
    e->x = 1;              // OK - unannotated pointer, unspecified mode
    (*e).x = 1;            // OK - unannotated pointer, unspecified mode
}

void test_unannotated_param_no_warn(Entity* p) {
    p->x = 1;              // OK - unannotated param, unspecified mode
}

// === Lambda scoping: analysis must still run for outer function ===
// Regression test: lambda bodies call ActOnStartOfFunctionDef, which must
// not clobber the per-function analysis decision for the enclosing function.

void test_lambda_does_not_clobber_outer(Entity* _Nullable p) {
    // Lambda with no annotations — should not disable outer analysis
    auto f = [](int x) { return x + 1; };
    (void)f(1);
    p->x = 1;              // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void test_nested_lambda_scoping(Entity* _Nullable p) {
    auto outer = [](int x) {
        auto inner = [](int y) { return y; };
        return inner(x);
    };
    (void)outer(1);
    (*p).x = 1;            // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

#pragma clang assume_nonnull end
