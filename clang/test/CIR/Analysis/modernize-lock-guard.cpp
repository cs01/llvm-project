// Opt-in refinement rewrite: m.lock() ... m.unlock() closing a block becomes
// a std::lock_guard.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lock-guard -fno-caret-diagnostics %s -o /dev/null 2>&1 | FileCheck %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lock-guard -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

struct Mutex {
  void lock();
  void unlock();
};
namespace std {
template <class M> struct lock_guard {
  M &m;
  explicit lock_guard(M &m) : m(m) { m.lock(); }
  ~lock_guard() { m.unlock(); }
};
} // namespace std

Mutex Mu;
int Count;

// CHECK: warning: the lock() and unlock() in 'bump' can be a std::lock_guard
// FIXED: void bump() {
// FIXED-NEXT: std::lock_guard<Mutex> guard_Mu(Mu);
// FIXED-NEXT: ++Count;
// FIXED-NEXT: }
void bump() {
  Mu.lock();
  ++Count;
  Mu.unlock();
}
// The block does not end with the unlock.
// CHECK-NOT: 'early'
int early(int x) {
  Mu.lock();
  if (x)
    return 1;
  Mu.unlock();
  return 0;
}
