// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fnullability-safety-cir -fnullability-safety-cir-facts=%t.json %s -o /dev/null
// RUN: FileCheck --input-file=%t.json %s

struct Node { int value; Node *next; };
[[noreturn]] void fail();
void log();
int straight(Node *p) { return p->value; }
int conditional(Node *p, bool f) { if (f) return p->value; return 0; }
int bothArms(Node *p, bool f) { if (f) p->value = 1; else p->value = 2; return 0; }
int afterLoop(Node *p, int k) { while (k) k--; return p->value; }
int inLoopBody(Node *p, int k) { int s = 0; while (k--) s += p->value; return s; }
int doBody(Node *p, int k) { do { k += p->value; } while (k < 3); return k; }
int earlyReturn(Node *p, int k) { if (k) return 0; return p->value; }
int failGuard(int k, Node *p) { if (k < 0) fail(); return p->value; }
int throwGuard(int k, Node *p) { if (k < 0) throw 1; return p->value; }
int callFirst(Node *p) { log(); return p->value; }
int exitInLoop(Node *p, int k) { while (k--) if (k == 7) fail(); return p->value; }
int viaCallee(Node *p) { return straight(p); }
struct S { int x; int get() const { return x; } };
int method(S *s) { return s->get(); }
int reassigned(Node *p) { p = p->next; return p->value; }
int indexed(int *a) { return a[1]; }
void takesAddr(Node **pp);
int addr(Node *p) { takesAddr(&p); return p->value; }
int shortCircuit(Node *p, bool f) { return f && p->value; }

// CHECK-LABEL: "demangled": "straight(Node*)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "conditional(Node*, bool)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "bothArms(Node*, bool)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "afterLoop(Node*, int)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "inLoopBody(Node*, int)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "doBody(Node*, int)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "earlyReturn(Node*, int)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "failGuard(int, Node*)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "throwGuard(int, Node*)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "callFirst(Node*)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "exitInLoop(Node*, int)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "viaCallee(Node*)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "method(S*)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "reassigned(Node*)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "indexed(int*)",
// CHECK:       "must_deref": true,

// CHECK-LABEL: "demangled": "addr(Node*)",
// CHECK:       "must_deref": false,

// CHECK-LABEL: "demangled": "shortCircuit(Node*, bool)",
// CHECK:       "must_deref": false,
