// Opt-in refinement rewrite: adjacent `T *p, size_t n` become one view when
// the body only indexes p and only reads n.

// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=span -fdiagnostics-parseable-fixits -fno-caret-diagnostics %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize %s -o /dev/null 2>&1 | FileCheck --check-prefix=DEFAULT --allow-empty %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=span -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++20 -triple x86_64-unknown-linux-gnu -fsyntax-only %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

// DEFAULT-NOT: can be one view

using size_t = decltype(sizeof(0));
namespace std {
template <class T> class span {
  T *d;
  size_t n;

public:
  span(T *d, size_t n) : d(d), n(n) {}
  size_t size() const { return n; }
  T &operator[](size_t i) const { return d[i]; }
};
} // namespace std

// CHECK: warning: pointer parameter 'p' of 'sum' and the length after it can be one view
// FIXED: static int sum(std::span<const int> p) {
// FIXED: for (size_t i = 0; i < p.size(); ++i)
static int sum(const int *p, size_t n) {
  int s = 0;
  for (size_t i = 0; i < n; ++i)
    s += p[i];
  return s;
}
// CHECK: warning: pointer parameter 'p' of 'fill' and the length after it
// FIXED: static void fill(std::span<int> p, int v) {
static void fill(int *p, size_t n, int v) {
  for (size_t i = 0; i != n; ++i)
    p[i] = v;
}
// CHECK-NOT: 'walk'
static int walk(const int *p, size_t n) { return n ? *p : 0; }
// CHECK-NOT: 'grow'
static int grow(const int *p, size_t n) { return p[++n]; }

int entry(int *a, size_t n) {
  // FIXED: fill(std::span<int>(a, n), 1);
  fill(a, n, 1);
  // FIXED: return sum(std::span<const int>(a, n)) + walk(a, n) + grow(a, n);
  return sum(a, n) + walk(a, n) + grow(a, n);
}
