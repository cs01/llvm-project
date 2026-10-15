// RUN: %clang_cc1 -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c11 -verify %s
// RUN: %clang_cc1 -triple aarch64-linux-gnu -target-feature +sve -fsyntax-only -fflow-sensitive-nullability -fnullability-default=nullable -Wno-nullable-to-nonnull-conversion -std=c11 -verify=sve %s
//
// The bool-coerced ternary in C, where assertion macros spell it with int
// arms: (c) ? 1 : 0.

#ifndef __ARM_FEATURE_SVE

struct Obj {
  int v;
};
struct Obj *Find(void);
void Fail(void) __attribute__((noreturn));

#define ASSERT(c)                                                              \
  do {                                                                         \
    if (!((c) ? 1 : 0))                                                        \
      Fail();                                                                  \
  } while (0)

int assert_pointer(void) {
  struct Obj *p = Find();
  ASSERT(p);
  return p->v;
}

int assert_and(void) {
  struct Obj *p = Find();
  struct Obj *q = Find();
  ASSERT(p && q);
  return p->v + q->v;
}

int guard_flag(void) {
  struct Obj *p = Find();
  struct Obj *q = Find();
  int ok = (p && q) ? 1 : 0;
  if (!ok)
    return 0;
  return p->v + q->v;
}

// Still reported.

int assert_or(void) {
  struct Obj *p = Find();
  struct Obj *q = Find();
  ASSERT(p || q);
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int truncated_arm(void) {
  struct Obj *p = Find();
  if (__builtin_expect((char)(p ? 0 : 256), 0))
    return 0;
  return p->v; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

#else

// sve-no-diagnostics
// An arm the ICE checker accepts but the evaluator cannot fold is not
// constant-evaluated.
int sizeless_arm(int c) {
  if (c ? __builtin_vectorelements(__SVFloat32_t) : 4)
    return 1;
  return 0;
}

#endif
