// A method whose returned pointer or reference can be derived from `this`
// can be [[clang::lifetimebound]], after its qualifiers.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound-this -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=orig %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound-this -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=fixed %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

// orig-no-diagnostics
namespace {
struct Box {
  int v;
  int *p;
  // CHECK: warning: the result of 'get' can refer to the object 'get' is called on
  // FIXED: const int &get() const {{\[\[}}clang::lifetimebound{{\]\]}} {
  const int &get() const { return v; }
  // CHECK: warning: the result of 'ref' can refer to the object 'ref' is called on
  // FIXED: int &ref() & noexcept {{\[\[}}clang::lifetimebound{{\]\]}} {
  int &ref() & noexcept { return v; }
  // The pointer is loaded from a member: it does not point into the object.
  // CHECK-NOT: 'ptr'
  int *ptr() { return p; }
  // CHECK-NOT: 'trail'
  auto trail() const -> const int & { return v; }
  // FIXED: const int &out() const {{\[\[}}clang::lifetimebound{{\]\]}};
  const int &out() const;
};
// CHECK: warning: the result of 'out' can refer to the object 'out' is called on
// FIXED: const int &Box::out() const {{\[\[}}clang::lifetimebound{{\]\]}} {
const int &Box::out() const { return v; }
} // namespace

// Box has internal linkage, so this does too; entry keeps it emitted.
static int use(Box &b) {
  const int &r = Box{1, nullptr}.get(); // fixed-warning {{temporary bound to local reference 'r' will be destroyed at the end of the full-expression}}
  return r + b.ref() + *b.ptr() + b.out() + b.trail();
}
int entry() {
  Box b{2, nullptr};
  return use(b);
}
