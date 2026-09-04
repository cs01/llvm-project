// A pointer dynamic_cast yields null whenever the runtime check fails, so its
// result is nullable in both nullability-default modes. Shared expectations
// use the expected prefix; mode-specific ones use nullable / nonnull.
//
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++17 -Rnullsafe-evidence %s -verify=expected,nullable
// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nonnull -Wno-nullable-to-nonnull-conversion -std=c++17 -Rnullsafe-evidence %s -verify=expected,nonnull

struct Base {
  virtual ~Base();
};
struct Derived : Base {
  int value;
};

void takesNonnull(Derived *_Nonnull);

Derived *returnDynamic(Base *_Nonnull p) {
  return dynamic_cast<Derived *>(p); // expected-remark{{returns nullable}}
}

Derived *returnStatic(Derived *_Nonnull p) { // expected-remark{{function 'returnStatic' always returns a non-null pointer}}
  return static_cast<Derived *>(p); // expected-remark{{returns nonnull}}
}

void dynamicCastPropagation(Base *_Nonnull p) {
  Derived *d = dynamic_cast<Derived *>(p);
  d->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  takesNonnull(dynamic_cast<Derived *>(p)); // expected-warning{{passing nullable pointer to nonnull parameter}} expected-note{{add a null check before the call}}
}

void preservingCastControls(Derived *_Nonnull p) {
  Derived *d = static_cast<Derived *>(p);
  d->value = 1;
  takesNonnull(static_cast<Derived *>(p));
}

void narrowedDynamicCastIsSafe(Base *_Nonnull p) {
  if (Derived *d = dynamic_cast<Derived *>(p))
    d->value = 1;
}

// returnDynamic's unannotated return type is what the caller sees, so only the
// nullable default reports this dereference.
void callerStillWarns(Base *_Nonnull p) {
  returnDynamic(p)->value = 1; // nullable-warning{{dereference of nullable pointer}} nullable-note{{add a null check}} expected-remark-re{{parameter 'p' of 'returnDynamic' (declared at {{.*}}) called with nonnull argument}}
}

// ===----------------------------------------------------------------------===//
// dynamic_cast initializer is nullable, never narrowing
// ===----------------------------------------------------------------------===//
// Under -fnullability-default=nonnull the cast's unannotated result type is
// not nullable by type, so the initializer itself must carry the taint;
// judging the (non-null) cast source would wrongly narrow the variable.

void initFromDynamicCastWarns(Base *_Nonnull p) {
  Derived *q = dynamic_cast<Derived *>(p);
  q->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void initInConditionNarrows(Base *_Nonnull p) {
  if (auto *q = dynamic_cast<Derived *>(p))
    q->value = 1;
}

void initThenCheckNarrows(Base *_Nonnull p) {
  Derived *q = dynamic_cast<Derived *>(p);
  if (q)
    q->value = 1;
}

void reassignmentClearsTaint(Base *_Nonnull p) {
  Derived local;
  Derived *q = dynamic_cast<Derived *>(p);
  q = &local;
  q->value = 1;
}

void nonnullInitFromDynamicCastWarns(Base *_Nonnull p) {
  Derived *_Nonnull q = dynamic_cast<Derived *>(p); // expected-warning{{assigning nullable pointer to nonnull variable}} expected-note{{add a null check before assigning}}
  q->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void assignFromDynamicCastWarns(Base *_Nonnull p) {
  Derived local;
  Derived *q = &local;
  q = dynamic_cast<Derived *>(p);
  q->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

struct Holder {
  Derived *d;
};

void memberAssignFromDynamicCastWarns(Holder &h, Base *_Nonnull p) {
  h.d = dynamic_cast<Derived *>(p); // expected-remark-re{{member 'd' of Holder (declared at {{.*}}) assigned from nullable source}}
  h.d->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

// ===----------------------------------------------------------------------===//
// A dynamic_cast nested under arithmetic or another cast is still the origin
// ===----------------------------------------------------------------------===//
// Unwrapping at a dereference site must stop at the dynamic_cast rather than
// judging its (non-null) source.

void derefDynamicCastPlusOne(Base *_Nonnull p) {
  (*(dynamic_cast<Derived *>(p) + 1)).value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void arrowDynamicCastPlusOne(Base *_Nonnull p) {
  (dynamic_cast<Derived *>(p) + 1)->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void staticCastOfDynamicCastPlusOne(Base *_Nonnull p) {
  static_cast<Derived *>(dynamic_cast<Derived *>(p) + 1)->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void staticCastOfDynamicCast(Base *_Nonnull p) {
  static_cast<Derived *>(dynamic_cast<Derived *>(p))->value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
  (*static_cast<Derived *>(dynamic_cast<Derived *>(p))).value = 2; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void starDynamicCast(Base *_Nonnull p) {
  (*dynamic_cast<Derived *>(p)).value = 1; // expected-warning{{dereference of nullable pointer}} expected-note{{add a null check}}
}

void narrowedDynamicCastArithmeticIsSafe(Base *_Nonnull p) {
  if (auto *q = dynamic_cast<Derived *>(p)) {
    (*(q + 1)).value = 1;
    (q + 1)->value = 2;
    static_cast<Derived *>(q + 1)->value = 3;
  }
}
