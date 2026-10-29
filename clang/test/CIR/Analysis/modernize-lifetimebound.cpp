// A pointer or reference parameter the returned value can be derived from,
// by address arithmetic alone, can be [[clang::lifetimebound]].

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound %s -o /dev/null 2>&1 | FileCheck --check-prefix=NEG %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=orig %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -verify=fixed %t.cpp

struct S { int a[4]; int x; S *next; };

// CHECK: :[[@LINE+2]]:33: warning: the result of 'pick' can refer to what parameter 's' refers to
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:34-[[@LINE+1]]:34}:" {{\[\[}}clang::lifetimebound{{\]\]}}"
static const int &pick(const S &s) { return s.x; }
// CHECK: warning: the result of 'elem' can refer to what parameter 's'
static int *elem(S *s, int i) { return &s->a[i]; }
// CHECK: warning: the result of 'either' can refer to what parameter 'a'
// CHECK: warning: the result of 'either' can refer to what parameter 'b'
static const int &either(const int &a, const int &b, bool c) {
  return c ? a : b;
}
// A pointer loaded from the pointee, or one that does not come from an
// argument, carries no origin.
// NEG-NOT: 'follow'
static S *follow(S *s) { return s->next; }
// NEG-NOT: 'fresh'
static int *fresh(int *) {
  static int g;
  return &g;
}

// An unnamed parameter takes the attribute in front, where it appertains to
// the parameter and not to its type.
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:31-[[@LINE+1]]:31}:"{{\[\[}}clang::lifetimebound{{\]\]}} "
static const int &unnamedDecl(const int &);
static const int &unnamedDecl(const int &x) { return x; }

int use(S *s, const int &a, const int &b) {
  return pick(*s) + *elem(s, 1) + follow(s)->x + either(a, b, true) +
         *fresh(nullptr) + unnamedDecl(a);
}

// The annotation is what lets clang see this dangling reference.
// orig-no-diagnostics
static int dangling() {
  const int &r = pick(S{}); // fixed-warning {{temporary bound to local reference 'r' will be destroyed at the end of the full-expression}}
  return r;
}
int callDangling() { return dangling(); }
