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

// A view returned by value: built from an owner the object holds, it points
// into the object; a stored view copied out does not.
struct [[gsl::Owner(char)]] Str {
  char *d;
  ~Str() {}
};
struct [[gsl::Pointer(char)]] Ref {
  const char *d;
  Ref(const Str &s) : d(s.d) {}
  Ref(const Ref &) = default;
};
struct Named {
  Str name;
  Ref alias;
  // CHECK: warning: the result of 'getName' can refer to the object 'getName' is called on
  // FIXED: Ref getName() const {{\[\[}}clang::lifetimebound{{\]\]}} {
  Ref getName() const { return name; }
  // CHECK-NOT: 'getAlias'
  Ref getAlias() const { return alias; }
};
} // namespace

// Box has internal linkage, so this does too; entry keeps it emitted.
static int use(Box &b) {
  const int &r = Box{1, nullptr}.get(); // fixed-warning {{temporary bound to local reference 'r' will be destroyed at the end of the full-expression}}
  return r + b.ref() + *b.ptr() + b.out() + b.trail();
}
static const char *views(Named &n) {
  Ref v = Named{Str{}, n.alias}.getName(); // fixed-warning {{temporary whose address is used as value of local variable 'v' will be destroyed at the end of the full-expression}}
  return v.d + *n.getAlias().d;
}
int entry() {
  Box b{2, nullptr};
  static Str Owner;
  Named n{Str{}, Ref(Owner)};
  return use(b) + *views(n);
}
