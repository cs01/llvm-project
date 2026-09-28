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
