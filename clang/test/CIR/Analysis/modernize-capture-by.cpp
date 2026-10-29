// A pointer, reference or view parameter stored into memory `this`, another
// parameter or a global refers to can be [[clang::lifetime_capture_by]].

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=capture-by -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=orig %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=capture-by -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=fixed %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

// orig-no-diagnostics
namespace {
struct [[gsl::Pointer(char)]] Ref {
  const char *d;
};
struct Holder {
  const int *p;
  Ref name;
  // A constructor keeping an argument is lifetimebound's case, not this one.
  // CHECK-NOT: 'Holder' keeps
  Holder(const int *x) : p(x), name{} {}
  Holder() = default;
  // CHECK: warning: 'set' keeps what parameter 'x' refers to
  // FIXED: void set(const int *x {{\[\[}}clang::lifetime_capture_by_this{{\]\]}}) { p = x; }
  void set(const int *x) { p = x; }
  // A view assigned with its copy assignment operator.
  // FIXED: void setName(Ref r {{\[\[}}clang::lifetime_capture_by_this{{\]\]}}) { name = r; }
  void setName(Ref r) { name = r; }
  // CHECK-NOT: 'local'
  void local(const int *x) {
    const int *y = x;
    (void)y;
  }
};
} // namespace

const int *G;
// FIXED: static void keep(const int *x {{\[\[}}clang::lifetime_capture_by_global{{\]\]}}) { G = x; }
static void keep(const int *x) { G = x; }
// FIXED: static void keepRef(const int &x {{\[\[}}clang::lifetime_capture_by_global{{\]\]}}) { G = &x; }
static void keepRef(const int &x) { G = &x; }
// FIXED: static void into(const int *x {{\[\[}}clang::lifetime_capture_by(h){{\]\]}}, Holder &h) { h.p = x; }
static void into(const int *x, Holder &h) { h.p = x; }

static int use(Holder &h, const int *a) {
  h.set(a);
  h.setName(Ref{});
  h.local(a);
  keep(a);
  keepRef(1); // fixed-warning {{object whose reference is captured will be destroyed at the end of the full-expression}}
  into(a, h);
  return 0;
}
int entry() {
  Holder h{};
  int a = 0;
  return use(h, &a);
}
