// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++23 -verify %s
//
// The CFG splits a bare && / || condition of an if, while or for into one
// block per operand, so the terminator's block evaluates only the rightmost
// operand. A condition with a temporary that has a destructor is built as one
// block where every operand has merged, and only facts that hold whichever
// operand ran last apply there.

struct Obj { int v; };
Obj *Find();
void take(Obj *_Nonnull p);

struct Str {
  ~Str();
  bool empty() const;
};
Str name();

int or_with_temporary() {
  Obj *p = Find();
  if (name().empty() || p)
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}

int and_with_temporary() {
  Obj *p = Find();
  if (!name().empty() && p)
    return p->v; // every operand held
  return 0;
}

int or_with_temporary_false_edge() {
  Obj *p = Find();
  if (name().empty() || !p)
    return 0;
  return p->v; // every operand was false
}

int and_negated_with_temporary() {
  Obj *p = Find();
  if (!name().empty() && !p)
    return 0;
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int while_with_temporary() {
  Obj *p = Find();
  int n = 0;
  while (name().empty() || p)
    n += p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return n;
}

// A split chain narrows on each operand's own edge, so the if does not
// re-apply a test that a later operand overwrote.
int split_and_overwritten() {
  Obj *p = Find();
  if (p && (p = nullptr, true))
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}

int split_and() {
  Obj *p = Find();
  Obj *q = Find();
  if (p && q)
    return p->v + q->v;
  return 0;
}

// An if consteval has no condition to narrow on.
int consteval_if(Obj *p) {
  if consteval {
    return 0;
  } else {
    take(p); // expected-warning {{passing nullable pointer to nonnull parameter 'p'}} expected-note {{add a null check before the call}}
    return 1;
  }
}
