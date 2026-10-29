// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const %s -o /dev/null 2>&1 | FileCheck --check-prefix=NEG %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=const -fcir-modernize-apply %t.cpp -o /dev/null
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fsyntax-only -Wno-unused-function %t.cpp
// RUN: FileCheck --check-prefix=FIXED --input-file=%t.cpp %s

struct Node {
  int value;
  Node *next;
};
struct S {
  int x;
  int get() const { return x; }
  void set(int v) { x = v; }
};
int takesConst(const Node *p);
int writer(Node *p);
Node *global;

static int readOnly(Node *p) { return p->value + p->next->value; }
static int viaConstCallee(Node *p) { return takesConst(p); }
static int forwards(Node *p) { return readOnly(p); }
static int constMethod(S *s) { return s->get(); }
static int copyStruct(Node *p) {
  Node c = *p;
  return c.value;
}
static bool isNull(Node *p) { return p == nullptr; }

static int writes(Node *p) {
  p->value = 1;
  return 0;
}
static int nonConstMethod(S *s) {
  s->set(1);
  return 0;
}
static int passesToWriter(Node *p) { return writer(p); }
static int escapes(Node *p) {
  global = p;
  return 0;
}
static Node *returnsIt(Node *p) { return p; }
static int localCopy(Node *p) {
  Node *q = p;
  return q->value;
}
static int forwardsToWriter(Node *p) { return writes(p); }
static int alreadyConst(const Node *p) { return p->value; }
int publicFn(Node *p) { return p->value; }

int use(Node *n, S *s) {
  return readOnly(n) + viaConstCallee(n) + forwards(n) + constMethod(s) +
         copyStruct(n) + isNull(n) + writes(n) + nonConstMethod(s) +
         passesToWriter(n) + escapes(n) + (returnsIt(n) != nullptr) +
         localCopy(n) + forwardsToWriter(n) + alreadyConst(n) + publicFn(n);
}

// CHECK-DAG: warning: pointer parameter 'p' of 'readOnly' is never written through; it can point to const
// CHECK-DAG: warning: pointer parameter 'p' of 'viaConstCallee'
// CHECK-DAG: warning: pointer parameter 'p' of 'forwards'
// CHECK-DAG: warning: pointer parameter 's' of 'constMethod'
// CHECK-DAG: warning: pointer parameter 'p' of 'copyStruct'
// CHECK-DAG: warning: pointer parameter 'p' of 'isNull'

// NEG-NOT: of 'writes'
// NEG-NOT: of 'nonConstMethod'
// NEG-NOT: of 'passesToWriter'
// NEG-NOT: of 'escapes'
// NEG-NOT: of 'returnsIt'
// NEG-NOT: of 'localCopy'
// NEG-NOT: of 'forwardsToWriter'
// NEG-NOT: of 'alreadyConst'
// NEG-NOT: of 'publicFn'

// FIXED: {{^}}static int readOnly(const Node *p) { return p->value + p->next->value; }
// FIXED: {{^}}static int viaConstCallee(const Node *p) { return takesConst(p); }
// FIXED: {{^}}static int forwards(const Node *p) { return readOnly(p); }
// FIXED: {{^}}static int constMethod(const S *s) { return s->get(); }
// FIXED: {{^}}static int copyStruct(const Node *p) {
// FIXED: {{^}}static bool isNull(const Node *p) { return p == nullptr; }
// FIXED: {{^}}static int writes(Node *p) {

// A deduction guide has no symbol; looking callees up by mangled name must
// not try to mangle it.
template <typename T> struct Box {
  Box(T *) {}
};
template <typename T> Box(T *) -> Box<T>;
int useBox(Node *n) {
  Box b(n);
  return 0;
}
