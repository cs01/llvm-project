// A method that never writes through `this`, and passes it only to what
// reads, can be const.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const-method -fdiagnostics-parseable-fixits -fno-caret-diagnostics %s -o /dev/null 2>&1 | FileCheck %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const-method -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -Wno-unused %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

namespace {
struct Counter {
  int n;
  int *p;
  // CHECK: warning: method 'get' never writes through 'this'; it can be const
  // FIXED: int get() const { return n; }
  int get() { return n; }
  // Calls only a const method.
  // CHECK: warning: method 'twice' never writes through 'this'
  // FIXED: int twice() const & { return get2() * 2; }
  int twice() & { return get2() * 2; }
  int get2() const { return n; }
  // Writing through a pointer member is not writing through `this`.
  // CHECK: warning: method 'poke' never writes through 'this'
  // FIXED: void poke() const;
  void poke();
  // CHECK-NOT: 'bump'
  void bump() { ++n; }
  // CHECK-NOT: 'callsBump'
  void callsBump() { bump(); }
  // Returning a reference into the object needs it non-const.
  // CHECK-NOT: 'ref'
  int &ref() { return n; }
};
// FIXED: void Counter::poke() const { *p = 1; }
void Counter::poke() { *p = 1; }
} // namespace

int entry() {
  int x = 0;
  Counter c{1, &x};
  c.poke();
  c.callsBump();
  return c.get() + c.twice() + c.ref();
}
