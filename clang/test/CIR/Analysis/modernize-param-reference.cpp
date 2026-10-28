// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize %s -o /dev/null 2>&1 | FileCheck --check-prefix=NEG %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fsyntax-only -Wno-unused-function %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

struct Node {
  int value;
  Node *next;
};
[[noreturn]] void fail();
void log();

static int first(Node *head) { return head->value; }
static int viaCallee(Node *p) { return first(p); }
static int deref(Node *p) { return (*p).value + p->next->value; }
struct S {
  int x;
  int get() const { return x; }
};
static int method(S *s) { return s->get(); }

static int conditional(Node *p, bool f) {
  if (f)
    return p->value;
  return 0;
}
static int failGuard(int k, Node *p) {
  if (k < 0)
    fail();
  return p->value;
}
static int throwGuard(int k, Node *p) {
  if (k < 0)
    throw 1;
  return p->value;
}
static int callFirst(Node *p) {
  log();
  return p->value;
}
static int sized(Node *p) { return p->value + sizeof(p); }
static int captured(Node *p) {
  int v = p->value;
  return v + [=] { return p->value; }();
}
static int nullCaller(Node *p) { return p->value; }
static int forwardsToUnsafe(Node *p) {
  int v = p->value;
  return v + conditional(p, true);
}
int publicFn(Node *p) { return p->value; }

int use(Node *n, Node &r, S *s) {
  Node local{1, nullptr};
  int total = first(n) + first(&local) + first(n->next) + viaCallee(&r);
  total += deref(n) + method(s) + conditional(n, true) + failGuard(1, n);
  total += throwGuard(1, n) + callFirst(n) + sized(n) + captured(n);
  total += nullCaller(nullptr) + forwardsToUnsafe(n) + publicFn(n);
  return total;
}

// CHECK-DAG: warning: pointer parameter 'head' of 'first' is dereferenced on every path and never null; it can be a reference
// CHECK-DAG: warning: pointer parameter 'p' of 'viaCallee'
// CHECK-DAG: warning: pointer parameter 'p' of 'deref'
// CHECK-DAG: warning: pointer parameter 's' of 'method'
// CHECK-DAG: fix-it:"{{.*}}":{15:23-15:24}:"&"
// CHECK-DAG: fix-it:"{{.*}}":{15:43-15:45}:"."

// NEG-NOT: of 'conditional'
// NEG-NOT: of 'failGuard'
// NEG-NOT: of 'throwGuard'
// NEG-NOT: of 'callFirst'
// NEG-NOT: of 'sized'
// NEG-NOT: of 'captured'
// NEG-NOT: of 'nullCaller'
// NEG-NOT: of 'forwardsToUnsafe'
// NEG-NOT: of 'publicFn'

// FIXED: {{^}}static int first(Node &head) { return head.value; }
// FIXED: {{^}}static int viaCallee(Node &p) { return first(p); }
// FIXED: {{^}}static int deref(Node &p) { return (p).value + p.next->value; }
// FIXED: {{^}}static int method(S &s) { return s.get(); }
// FIXED: {{^}}  int total = first(*n) + first(local) + first(*n->next) + viaCallee(r);
// FIXED: {{^}}  total += deref(*n) + method(*s) + conditional(n, true) + failGuard(1, n);
// FIXED: {{^}}  total += throwGuard(1, n) + callFirst(n) + sized(n) + captured(n);
