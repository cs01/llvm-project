// Smart pointers shaped like libc++ in C++20 mode: shared_ptr declares only
// operator==(const shared_ptr &, nullptr_t), so sp != nullptr and
// nullptr == sp are rewritten comparisons, and operator->, operator* and
// operator bool are members of shared_ptr itself. Most cases take a
// _Nullable parameter so that they mean the same in both default modes; an
// unannotated, unchecked smart pointer is trusted under the nonnull default.
//
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nullable -std=c++20 %s -verify=expected,nullable
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -fnullability-default=nonnull -std=c++20 %s -verify=expected

namespace std {
using nullptr_t = decltype(nullptr);
template <class T> T &&move(T &) noexcept;
template <class T> class shared_ptr {
public:
  shared_ptr() noexcept;
  shared_ptr(nullptr_t) noexcept;
  shared_ptr(const shared_ptr &) noexcept;
  shared_ptr(shared_ptr &&) noexcept;
  ~shared_ptr();
  shared_ptr &operator=(const shared_ptr &) noexcept;
  shared_ptr &operator=(shared_ptr &&) noexcept;
  T *get() const noexcept;
  T &operator*() const noexcept;
  T *operator->() const noexcept;
  explicit operator bool() const noexcept;
  void reset() noexcept;
};
template <class T>
bool operator==(const shared_ptr<T> &, nullptr_t) noexcept;
} // namespace std

struct S {
  int x;
};

[[noreturn]] void fatal();

//===----------------------------------------------------------------------===//
// S0: a declaration runs once per loop iteration and creates a new smart
// pointer, so a move or reset at the end of one iteration does not reach the
// next iteration's declaration.
//===----------------------------------------------------------------------===//

std::shared_ptr<S> s0_make();
std::shared_ptr<S> _Nullable s0_make_nullable();
void s0_sink(std::shared_ptr<S>);

void s0_moved_at_end(int n) {
  for (int i = 0; i < n; ++i) {
    std::shared_ptr<S> p = s0_make();
    p->x = i; // nullable-warning {{dereference of nullable pointer}} nullable-note {{add a null check}}
    s0_sink(std::move(p));
  }
}

void s0_checked_then_reset(int n) {
  for (int i = 0; i < n; ++i) {
    std::shared_ptr<S> p = s0_make();
    if (!p)
      continue;
    p->x = i;
    p.reset();
  }
}

void s0_nullable_still_warns(int n) {
  for (int i = 0; i < n; ++i) {
    std::shared_ptr<S> p = s0_make_nullable();
    p->x = i; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
    p.reset();
  }
}

// The moved-from pointer itself stays tainted until the loop redeclares it.
int s0_used_after_loop_body(int n) {
  std::shared_ptr<S> p = s0_make();
  for (int i = 0; i < n; ++i)
    s0_sink(std::move(p));
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

//===----------------------------------------------------------------------===//
// S4: the rewritten operator can sit anywhere in a chain of !, as in the
// expansions of assertion macros.
//===----------------------------------------------------------------------===//

int s4_not_ne(std::shared_ptr<S> _Nullable p) {
  if (!(p != nullptr))
    return 0;
  return p->x;
}

int s4_assert_trap(std::shared_ptr<S> _Nullable p) {
  do {
    if (!(p != nullptr))
      __builtin_trap();
  } while (0);
  return p->x;
}

int s4_check_expect(std::shared_ptr<S> _Nullable p) {
  while (__builtin_expect(!!(!((p) != nullptr)), 0))
    fatal();
  return p->x;
}

int s4_negated_expect(std::shared_ptr<S> _Nullable p) {
  if (!__builtin_expect(p != nullptr, 1))
    return 0;
  return p->x;
}

int s4_not_not_ne(std::shared_ptr<S> _Nullable p) {
  if (!!(p != nullptr))
    return (*p).x;
  return 0;
}

int s4_not_reversed_ne(std::shared_ptr<S> _Nullable p) {
  if (!(nullptr != p))
    return 0;
  return p->x;
}

int s4_guard(std::shared_ptr<S> _Nullable p) {
  const bool missing = !(p != nullptr);
  if (missing)
    return 0;
  return p->x;
}

int s4_unannotated(std::shared_ptr<S> p) {
  if (!(p != nullptr))
    return 0;
  return p->x;
}

int s4_ne(std::shared_ptr<S> _Nullable p) {
  if (p != nullptr)
    return p->x;
  return 0;
}

int s4_not_eq(std::shared_ptr<S> _Nullable p) {
  if (!(p == nullptr))
    return p->x;
  return 0;
}

int s4_wrong_way(std::shared_ptr<S> _Nullable p) {
  if (!(p != nullptr))
    return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  return 0;
}

int s4_guard_wrong_way(std::shared_ptr<S> _Nullable p) {
  const bool missing = !(p != nullptr);
  if (!missing)
    return 0;
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

//===----------------------------------------------------------------------===//
// S2: a smart pointer reached through a reference (a reference parameter, a
// local reference, a reference member) is tracked like one held by value:
// null checks narrow it, and reset(), assignment and std::move through the
// reference drop the narrowing.
//===----------------------------------------------------------------------===//

std::shared_ptr<S> lookup(int id);
std::shared_ptr<S> _Nullable lookup_nullable(int id);
void consume(std::shared_ptr<S>);
void mutate();

struct RefHolder {
  const std::shared_ptr<S> _Nullable &r;
  const std::shared_ptr<S> &u;
};

int s2_eq_cref(const std::shared_ptr<S> _Nullable &p) {
  if (p == nullptr)
    return 0;
  return p->x;
}

int s2_ne_cref(const std::shared_ptr<S> _Nullable &p) {
  if (p != nullptr)
    return (*p).x;
  return 0;
}

int s2_reversed_eq_cref(const std::shared_ptr<S> _Nullable &p) {
  if (nullptr == p)
    return 0;
  return p->x;
}

int s2_or_cref(const std::shared_ptr<S> _Nullable &a,
               const std::shared_ptr<S> _Nullable &b) {
  if (a == nullptr || b == nullptr)
    return 0;
  return a->x + b->x;
}

int s2_eq_local_ref(std::shared_ptr<S> _Nullable q) {
  const auto &p = q;
  if (p == nullptr)
    return 0;
  return p->x + q->x;
}

int s2_eq_ref_member(RefHolder h) {
  if (h.r == nullptr)
    return 0;
  return h.r->x;
}

int s2_unchecked_ref_member(RefHolder h) {
  return h.r->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_unannotated_ref_member(RefHolder h) {
  return h.u->x; // nullable-warning {{dereference of nullable pointer}} nullable-note {{add a null check}}
}

int s2_unchecked_cref(const std::shared_ptr<S> &p) {
  return p->x; // nullable-warning {{dereference of nullable pointer}} nullable-note {{add a null check}}
}

int s2_reset_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  p.reset();
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_assign_null_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  p = nullptr;
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_assign_nullable_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  p = lookup_nullable(1);
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_assign_unannotated_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  p = lookup(1);
  return p->x; // nullable-warning {{dereference of nullable pointer}} nullable-note {{add a null check}}
}

int s2_moved_from_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  consume(std::move(p));
  return p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_move_construct_from_ref(std::shared_ptr<S> &p) {
  if (!p)
    return 0;
  auto q = std::move(p);
  int a = q->x;
  return a + p->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int s2_move_assign_between_refs(std::shared_ptr<S> &p, std::shared_ptr<S> &q) {
  if (!q)
    return 0;
  p = std::move(q);
  int a = p->x;
  return a + q->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// Each iteration binds the reference anew, so a move at the end of one
// iteration does not reach the next one (S0).
void s2_range_for_ref_moved(std::shared_ptr<S> (&arr)[4]) {
  for (auto &q : arr) {
    q->x = 1; // nullable-warning {{dereference of nullable pointer}} nullable-note {{add a null check}}
    consume(std::move(q));
  }
}

// A class deriving from shared_ptr reaches operator== and reset() through a
// derived-to-base cast of its object.
struct DerivedPtr : std::shared_ptr<S> {};

int s2_derived_eq(DerivedPtr d) {
  if (d == nullptr)
    return 0;
  return d->x;
}

int s2_derived_reset(DerivedPtr d) {
  if (!d)
    return 0;
  d.reset();
  return d->x; // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// Known miss, shared with member paths: a call is not assumed to change what
// a reference refers to, so mutate() resetting p goes unnoticed.
int s2_ref_across_call(std::shared_ptr<S> _Nullable &p) {
  if (!p)
    return 0;
  mutate();
  return p->x;
}
