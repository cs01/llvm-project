// Opt-in refinement rewrite: a local new whose block ends with its delete
// becomes a std::unique_ptr.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=unique-ptr -fno-caret-diagnostics %s -o /dev/null 2>&1 | FileCheck %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=unique-ptr -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

namespace std {
template <class T> struct unique_ptr {
  T *p;
  explicit unique_ptr(T *p) : p(p) {}
  ~unique_ptr() { delete p; }
  T *operator->() const { return p; }
  T &operator*() const { return *p; }
};
} // namespace std
struct T {
  int v;
  T(int v) : v(v) {}
};
void keep(T *);

// CHECK: warning: the new and delete in 'clean' can be a std::unique_ptr
// FIXED: int clean(int x) {
// FIXED-NEXT: std::unique_ptr<T> p(new T(x));
// FIXED-NEXT: int r = p->v + (*p).v;
// FIXED-NEXT: return r;
// FIXED-NEXT: }
int clean(int x) {
  T *p = new T(x);
  int r = p->v + (*p).v;
  delete p;
  return r;
}
// The pointer is passed on.
// CHECK-NOT: 'escapes'
void escapes(int x) {
  T *p = new T(x);
  keep(p);
  delete p;
}
// The return names it.
// CHECK-NOT: 'named'
bool named(int x) {
  T *p = new T(x);
  delete p;
  return p != nullptr;
}
