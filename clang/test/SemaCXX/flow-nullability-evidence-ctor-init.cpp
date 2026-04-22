// Tests for member evidence emitted from C++ constructor initializer lists.
// Constructor initializers (': field(expr)') are CXXCtorInitializer nodes,
// not BinaryOperator assignments, so they need dedicated evidence emission.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 -Rnullsafe-evidence %s -verify

// ===----------------------------------------------------------------------===//
// Basic: nullable parameter -> nullable member evidence
// ===----------------------------------------------------------------------===//

struct Basic {
    int *ptr;
    Basic(int *p) : ptr(p) {} // expected-remark-re{{member 'ptr' of Basic (declared at {{.*}}) assigned from nullable source}}
};

// ===----------------------------------------------------------------------===//
// Nonnull parameter (_Nonnull annotation) -> nonnull member evidence
// ===----------------------------------------------------------------------===//

struct NonnullParam {
    int *ptr;
    NonnullParam(int * _Nonnull p) : ptr(p) {} // expected-remark-re{{member 'ptr' of NonnullParam (declared at {{.*}}) assigned from nonnull source}}
};

// ===----------------------------------------------------------------------===//
// Multiple members in initializer list
// ===----------------------------------------------------------------------===//

struct Multi {
    int *a;
    int *b;
    int *c;
    Multi(int *x, int * _Nonnull y, int *z)
        : a(x),  // expected-remark-re{{member 'a' of Multi (declared at {{.*}}) assigned from nullable source}}
          b(y),  // expected-remark-re{{member 'b' of Multi (declared at {{.*}}) assigned from nonnull source}}
          c(z) {} // expected-remark-re{{member 'c' of Multi (declared at {{.*}}) assigned from nullable source}}
};

// ===----------------------------------------------------------------------===//
// Non-pointer members are skipped (no evidence for int, etc.)
// ===----------------------------------------------------------------------===//

struct NonPointer {
    int val;
    int *ptr;
    NonPointer(int v, int *p)
        : val(v),
          ptr(p) {} // expected-remark-re{{member 'ptr' of NonPointer (declared at {{.*}}) assigned from nullable source}}
};

// ===----------------------------------------------------------------------===//
// address-of (&x) is nonnull evidence
// ===----------------------------------------------------------------------===//

struct AddrOf {
    int *ptr;
    int x;
    AddrOf() : ptr(&x) {} // expected-remark-re{{member 'ptr' of AddrOf (declared at {{.*}}) assigned from nonnull source}}
};

// ===----------------------------------------------------------------------===//
// new expression is nonnull evidence
// ===----------------------------------------------------------------------===//

struct NewExpr {
    int *ptr;
    NewExpr() : ptr(new int(42)) {} // expected-remark-re{{member 'ptr' of NewExpr (declared at {{.*}}) assigned from nonnull source}}
};

// ===----------------------------------------------------------------------===//
// this pointer is nonnull evidence
// ===----------------------------------------------------------------------===//

struct Base {
    int x;
};

struct UsesThis : Base {
    Base *self;
    UsesThis() : self(this) {} // expected-remark-re{{member 'self' of UsesThis (declared at {{.*}}) assigned from nonnull source}}
};

// ===----------------------------------------------------------------------===//
// __attribute__((nonnull)) on constructor -> nonnull evidence for params
// ===----------------------------------------------------------------------===//

struct AttrNonnull {
    int *ptr;
    __attribute__((nonnull))
    AttrNonnull(int *p) : ptr(p) {} // expected-remark-re{{member 'ptr' of AttrNonnull (declared at {{.*}}) assigned from nonnull source}}
};

// ===----------------------------------------------------------------------===//
// nullptr initializer is nullable evidence
// ===----------------------------------------------------------------------===//

struct NullInit {
    int *ptr;
    NullInit() : ptr(nullptr) {} // expected-remark-re{{member 'ptr' of NullInit (declared at {{.*}}) assigned from nullable source}}
};

// ===----------------------------------------------------------------------===//
// Instantiation call sites (ensure no crashes, evidence still emitted)
// ===----------------------------------------------------------------------===//

void test_instantiations() {
    int x = 0;
    Basic b(&x);
    NonnullParam np(&x);
    Multi m(&x, &x, &x);
    NonPointer npt(1, &x);
    AddrOf ao;
    NewExpr ne;
    UsesThis ut;
    AttrNonnull an(&x);
    NullInit ni;
}
