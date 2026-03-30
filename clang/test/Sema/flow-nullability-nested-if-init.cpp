// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -std=c++17 %s -verify

// BUG: && narrowing of a pointer is lost in specific combinations of
// if-init with dyn_cast, forward-declared types in template args,
// and nested struct return types. The && correctly guards the
// dereference but the analysis fails to narrow.

#pragma clang assume_nullable begin

template<typename K, typename V> struct DenseMap {
    struct Iter { V second; bool operator!=(Iter o) const; };
    Iter find(K key) const;
    Iter end() const;
};

struct VarDecl;
using BoolGuardMap = DenseMap<const VarDecl *, int>;

struct VarDecl {
    struct QT { bool isPointerType() const; bool isBooleanType() const; };
    QT getType() const;
};

struct Expr {};
template<typename T, typename U> T *dyn_cast(U *);

void and_narrowing_false_positive(const Expr *E, const BoolGuardMap *BoolGuards) {
    if (auto *VD = dyn_cast<VarDecl, const Expr>(E)) {
        if (BoolGuards && VD->getType().isBooleanType()) {
            BoolGuards->find(VD); // expected-warning {{dereference of nullable pointer}}
            // expected-note@-1 {{add a null check}}
        }
    }
}

#pragma clang assume_nullable end
