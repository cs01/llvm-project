// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -std=c++17 %s -verify
// expected-no-diagnostics

// Regression test: && narrowing was lost at the IfStmt merge point
// when the if-condition was a && chain. The false edge of the left
// operand merged with the true path, dropping the narrowing.

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

void and_narrowing_fixed(const Expr *E, const BoolGuardMap *BoolGuards) {
    if (auto *VD = dyn_cast<VarDecl, const Expr>(E)) {
        if (VD->getType().isPointerType())
            return;
        if (BoolGuards && VD->getType().isBooleanType()) {
            BoolGuards->find(VD); // OK — BoolGuards narrowed by &&
            (void)BoolGuards->end(); // OK
        }
    }
}

#pragma clang assume_nullable end
