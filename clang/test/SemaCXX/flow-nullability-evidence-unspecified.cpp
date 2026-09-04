// Tests that evidence emission distinguishes explicit _Nullable from
// null-unspecified (unannotated) sources. Unannotated pointers defaulted to
// nullable by -fnullability-default=nullable should NOT produce "nullable"
// evidence — only explicitly _Nullable or nullptr should.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 -Rnullsafe-evidence %s -verify

// ===----------------------------------------------------------------------===//
// Parameter evidence: explicit _Nullable argument -> "called with nullable"
// ===----------------------------------------------------------------------===//

void takes_ptr(int *p);

void pass_explicit_nullable(int * _Nullable np) {
    takes_ptr(np); // expected-remark-re{{parameter 'p' of 'takes_ptr' (declared at {{.*}}) called with nullable argument}}
}

// ===----------------------------------------------------------------------===//
// Parameter evidence: nullptr -> "called with nullable"
// ===----------------------------------------------------------------------===//

void pass_nullptr() {
    takes_ptr(nullptr); // expected-remark-re{{parameter 'p' of 'takes_ptr' (declared at {{.*}}) called with nullable argument}}
}

// ===----------------------------------------------------------------------===//
// Parameter evidence: unannotated pointer -> NO evidence emitted
// ===----------------------------------------------------------------------===//

void pass_unannotated(int *p) {
    takes_ptr(p); // no remark — p is null-unspecified, not explicitly _Nullable
}

// ===----------------------------------------------------------------------===//
// Parameter evidence: _Nonnull argument -> "called with nonnull" (unchanged)
// ===----------------------------------------------------------------------===//

void pass_nonnull(int * _Nonnull p) {
    takes_ptr(p); // expected-remark-re{{parameter 'p' of 'takes_ptr' (declared at {{.*}}) called with nonnull argument}}
}

// ===----------------------------------------------------------------------===//
// Return evidence: explicit _Nullable return -> "returns nullable"
// ===----------------------------------------------------------------------===//

int * _Nullable get_nullable();

int *return_explicit_nullable() {
    return get_nullable(); // expected-remark-re{{function 'return_explicit_nullable' of global scope (declared at {{.*}}) returns nullable}}
}

// ===----------------------------------------------------------------------===//
// Return evidence: nullptr return -> "returns nullable"
// ===----------------------------------------------------------------------===//

int *return_nullptr() {
    return nullptr; // expected-remark-re{{function 'return_nullptr' of global scope (declared at {{.*}}) returns nullable}}
}

// ===----------------------------------------------------------------------===//
// Return evidence: unannotated pointer return -> NO evidence emitted
// ===----------------------------------------------------------------------===//

int *get_unannotated();

int *return_unannotated() {
    return get_unannotated(); // no remark — return value is null-unspecified
}

// ===----------------------------------------------------------------------===//
// Return evidence: _Nonnull return -> "returns nonnull" (unchanged)
// ===----------------------------------------------------------------------===//

int *return_nonnull(int * _Nonnull p) { // expected-remark{{function 'return_nonnull' always returns a non-null pointer}}
    return p; // expected-remark-re{{function 'return_nonnull' of global scope (declared at {{.*}}) returns nonnull}}
}

// ===----------------------------------------------------------------------===//
// Member evidence: assignment from unannotated -> NO evidence
// ===----------------------------------------------------------------------===//

struct S {
    int *field;
};

void assign_unannotated(S *_Nonnull s, int *p) {
    s->field = p; // no remark — p is null-unspecified
}

// ===----------------------------------------------------------------------===//
// Member evidence: assignment from _Nullable -> "assigned from nullable"
// ===----------------------------------------------------------------------===//

void assign_nullable(S *_Nonnull s, int * _Nullable p) {
    s->field = p; // expected-remark-re{{member 'field' of S (declared at {{.*}}) assigned from nullable source}}
}

// ===----------------------------------------------------------------------===//
// Member evidence: assignment from nullptr -> "assigned from nullable"
// ===----------------------------------------------------------------------===//

void assign_nullptr(S *_Nonnull s) {
    s->field = nullptr; // expected-remark-re{{member 'field' of S (declared at {{.*}}) assigned from nullable source}}
}

// ===----------------------------------------------------------------------===//
// Member evidence: assignment from _Nonnull -> "assigned from nonnull"
// ===----------------------------------------------------------------------===//

void assign_nonnull(S *_Nonnull s, int * _Nonnull p) {
    s->field = p; // expected-remark-re{{member 'field' of S (declared at {{.*}}) assigned from nonnull source}}
}

// ===----------------------------------------------------------------------===//
// Parameter evidence: nonnull-parameter narrowing runs before evidence
// ===----------------------------------------------------------------------===//
// Only the second parameter is _Nonnull, but surviving the call proves p
// non-null, so the evidence for the first parameter must be nonnull too.
// If evidence were emitted before the narrowing pass, 'a' would get no
// remark at all (p is null-unspecified).

void two_params(int *a, int *_Nonnull b);

void call_two_params(int *p) {
    two_params(p, p); // expected-warning{{passing nullable pointer to nonnull parameter}} expected-note{{add a null check before the call}} expected-remark-re{{parameter 'a' of 'two_params' (declared at {{.*}}) called with nonnull argument}} expected-remark-re{{parameter 'b' of 'two_params' (declared at {{.*}}) called with nonnull argument}}
}

// ===----------------------------------------------------------------------===//
// Flow taint on a _Nonnull local is provable nullable evidence
// ===----------------------------------------------------------------------===//
// The declared _Nonnull does not win over an assignment of null: the argument
// and return evidence say nullable, and no all-returns-nonnull summary is
// inferred.

void pass_tainted_nonnull() {
    int *_Nonnull a = nullptr; // expected-warning{{null assigned to a variable of nonnull type}} expected-warning{{assigning nullable pointer to nonnull variable}} expected-note{{add a null check before assigning}}
    takes_ptr(a); // expected-remark-re{{parameter 'p' of 'takes_ptr' (declared at {{.*}}) called with nullable argument}}
}

int *return_tainted_nonnull() {
    int *_Nonnull a = nullptr; // expected-warning{{null assigned to a variable of nonnull type}} expected-warning{{assigning nullable pointer to nonnull variable}} expected-note{{add a null check before assigning}}
    return a; // expected-remark-re{{function 'return_tainted_nonnull' of global scope (declared at {{.*}}) returns nullable}}
}
