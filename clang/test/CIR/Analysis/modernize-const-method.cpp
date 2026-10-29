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

// A method that calls another non-const method on `this` is right once that
// one is const too; a write anywhere down the chain keeps both.
namespace {
struct Chain {
  int n;
  // CHECK-DAG: warning: method 'leaf' never writes through 'this'
  // CHECK-DAG: warning: method 'viaLeaf' never writes through 'this'
  // FIXED: int leaf() const { return n; }
  // FIXED: int viaLeaf() const { return leaf() + 1; }
  int leaf() { return n; }
  int viaLeaf() { return leaf() + 1; }
  void set() { n = 0; }
  int viaSet() { set(); return n; }
};
} // namespace
// CHECK-NOT: method 'viaSet'
int chain() {
  Chain c{1};
  return c.viaLeaf() + c.viaSet();
}

// A virtual method's override set changes together: every override proven,
// pure ones included; one override that writes keeps the whole set.
namespace {
struct Shape {
  // FIXED: virtual int area() const = 0;
  virtual int area() = 0;
  // FIXED: virtual int id() { return 0; }
  virtual int id() { return 0; }
  virtual ~Shape() {}
};
struct Square : Shape {
  int s, calls;
  // FIXED: int area() const override { return s * s; }
  int area() override { return s * s; }
  int id() override { return ++calls; }
};
} // namespace
// CHECK-DAG: warning: method 'area' never writes through 'this'
// CHECK-NOT: method 'id' never
int shapes() {
  Square q;
  q.s = 2;
  Shape &s = q;
  return s.area() + s.id() + q.area();
}

// A member of a class template is proven by every instantiation; one that
// writes in any instantiation stays.
namespace {
template <typename T> struct Cell {
  T v;
  // CHECK-DAG: warning: method 'get' never writes through 'this'
  // FIXED: T get() const { return v; }
  T get() { return v; }
  // CHECK-DAG: warning: method 'twiceGet' never writes through 'this'
  // FIXED: T twiceGet() const { return get() + get(); }
  T twiceGet() { return get() + get(); }
  void set(T x) { v = x; }
};
} // namespace
// CHECK-NOT: method 'set' never
int cells() {
  Cell<int> a{1};
  Cell<long> b{2};
  a.set(3);
  return a.twiceGet() + (int)b.get();
}
