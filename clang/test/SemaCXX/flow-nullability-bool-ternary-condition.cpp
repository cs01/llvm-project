// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c++20 -verify %s
//
// A condition spelled (c) ? true : false, as assertion macros often spell it,
// narrows like if (c). Swapped arms are !c. A ternary is only seen through when
// its arms are the literals true/false or 1/0 and differ.

namespace std {
template <class T> struct unique_ptr {
  T *operator->() const;
  explicit operator bool() const;
  void reset();
};
} // namespace std

struct Obj { int v; };
Obj *Find();
[[noreturn]] void Fail();

// An assertion macro whose failure path is a [[noreturn]] call.
#define ASSERT(c)                                                              \
  switch (0)                                                                   \
  case 0:                                                                      \
  default:                                                                     \
    if (!((c) ? true : false))                                                 \
      Fail();                                                                  \
    else [[likely]]                                                            \
      (void)0

// The pointer cannot be null at the dereference: no warning.
int assert_pointer(void) { Obj *p = Find(); ASSERT(p); return p->v; }
int assert_compare(void) { Obj *p = Find(); ASSERT(p != nullptr); return p->v; }
int assert_and(void) { Obj *p = Find(); Obj *q = Find(); ASSERT(p && q); return p->v + q->v; }
int assert_guard(void) { Obj *p = Find(); bool ok = p != nullptr; ASSERT(ok); return p->v; }
int assert_smart(std::unique_ptr<Obj> &sp) { ASSERT(sp); return sp->v; }
int ternary_if(void) { Obj *p = Find(); if (p ? true : false) return p->v; return 0; }
int ternary_int_arms(void) { Obj *p = Find(); if (!(p ? 1 : 0)) return 0; return p->v; }
int ternary_swapped_arms(void) { Obj *p = Find(); if (p ? false : true) return 0; return p->v; }
int negated_swapped_arms(void) { Obj *p = Find(); if (!(p ? false : true)) return p->v; return 0; }
int guard_from_and(void) { Obj *p = Find(); Obj *q = Find(); bool ok = (p && q) ? true : false; ASSERT(ok); return p->v + q->v; }
int assert_retested(void) { Obj *p = Find(); ASSERT(p && (p = Find())); return p->v; }

// The pointer can be null at the dereference: still reported.
int assert_other_pointer(void) {
  Obj *p = Find();
  Obj *q = Find();
  ASSERT(q);
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int assert_null(void) {
  Obj *p = Find();
  ASSERT(!p);
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int assert_or(void) {
  Obj *p = Find();
  Obj *q = Find();
  ASSERT(p || q);
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int negated_conjunct(void) {
  Obj *p = Find();
  Obj *q = Find();
  if ((q && !p) ? true : false)
    return 0;
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int smart_failed_edge(std::unique_ptr<Obj> &sp) {
  if (sp ? true : false)
    return 0;
  return sp->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int swapped_arms_then(void) {
  Obj *p = Find();
  if (p ? false : true)
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}
int same_arms(void) {
  Obj *p = Find();
  if (p ? true : true)
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}
int non_constant_arm(bool b) {
  Obj *p = Find();
  if (p ? true : b)
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}

int negated_swapped_arms_else(void) {
  Obj *p = Find();
  if (!(p ? false : true))
    return 0;
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// A later conjunct that changes the pointer leaves the earlier test stale.
int assert_overwritten(void) {
  Obj *p = Find();
  ASSERT(p && (p = nullptr, true));
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
int assert_reset(std::unique_ptr<Obj> &sp) {
  ASSERT(sp && (sp.reset(), true));
  return sp->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// Only literal arms count: (char)256 is 0, so this branch is never taken.
int truncated_arm(void) {
  Obj *p = Find();
  if (__builtin_expect((char)(p ? 0 : 256), 0))
    return 0;
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// The ternary as the last operand of a condition whose operands merge into one
// block (a temporary with a destructor, any do-while).
struct Str {
  ~Str();
  bool empty() const;
};
Str name();
int or_with_temporary(void) {
  Obj *p = Find();
  if (name().empty() || (p ? true : false))
    return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}
Obj *retry(int n) {
  Obj *p;
  do {
    p = Find();
  } while (--n > 0 && !(p ? true : false));
  return p;
}
int use_retry(void) {
  return retry(3)->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
