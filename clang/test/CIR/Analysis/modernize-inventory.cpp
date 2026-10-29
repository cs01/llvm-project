// Project mode lists every unsafe site in the translation unit's own source,
// with the state this translation unit can settle.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-export=%t.json %s -o /dev/null -Wno-array-bounds
// RUN: FileCheck --input-file=%t.json %s

struct mutex { void lock(); void unlock(); };
struct Flag { operator bool() const; int operator()(int) const; };

// CHECK:      "category": "raw pointer parameter",
// CHECK-NEXT: "row": "null",
// CHECK-NEXT: "state": "unknown",
// CHECK-NEXT: "usr": "c:@F@sum#*I#I#",
// CHECK-NEXT: "index": 0
// CHECK:      "category": "C array index",
// CHECK-NEXT: "row": "bounds",
// CHECK-NEXT: "state": "proven",
// CHECK-NEXT: "why": "constant index in bounds"
// CHECK:      "category": "C array index",
// CHECK-NEXT: "row": "bounds",
// CHECK-NEXT: "state": "outside",
// CHECK-NEXT: "why": "constant index out of bounds"
// CHECK:      "category": "pointer arithmetic",
int sum(int *p, int n) {
  int a[4] = {};
  int s = a[1] + a[4] + *(p + n);
  return s;
}

// A conversion operator and operator() are member calls with no identifier.
// CHECK:      "category": "manual lock",
// CHECK-NEXT: "row": "races",
// CHECK-NOT:  "category": "manual lock",
// CHECK:      "category": "uninitialized local",
int guarded(mutex &m, const Flag &f) {
  m.lock();
  int r;
  r = f ? f(1) : 0;
  return r;
}

// A reference member set from a parameter is settled by project mode from the
// lifetimebound rewrite; one already annotated is covered, and one bound to a
// by-value parameter dangles.
// CHECK:      "category": "reference member",
// CHECK-NEXT: "row": "lifetime",
// CHECK-NEXT: "state": "unknown",
// CHECK-NEXT: "usr": "c:@S@Holder@F@Holder#&1I#",
// CHECK-NEXT: "index": 0
// CHECK:      "category": "reference member",
// CHECK-NEXT: "row": "lifetime",
// CHECK-NEXT: "state": "covered",
// CHECK:      "category": "reference member",
// CHECK-NEXT: "row": "lifetime",
// CHECK-NEXT: "state": "outside",
// CHECK-NEXT: "why": "bound to a by-value parameter"
struct Holder {
  const int &r;
  Holder(const int &x) : r(x) {}
  Holder(const int &x [[clang::lifetimebound]], int) : r(x) {}
  Holder(int x, char) : r(x) {}
};
