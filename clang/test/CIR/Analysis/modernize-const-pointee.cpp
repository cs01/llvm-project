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

// A pointer to pointer: const goes after the pointee's own '*'.
// FIXED: {{^}}static int firstChar(const char *const *argv) { return argv[0][0]; }
static int firstChar(const char **argv) { return argv[0][0]; }
int useArgv(const char **argv) { return firstChar(argv); }

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

// Pointer difference and a memcpy source only read; a memcpy destination is
// written.
// CHECK-DAG: warning: pointer parameter 'b' of 'distance'
// CHECK-DAG: warning: pointer parameter 'src' of 'copyOut'
// NEG-NOT: parameter 'dst' of 'copyOut'
// FIXED: {{^}}static long distance(const char *b, const char *e) { return e - b; }
// FIXED: {{^}}static void copyOut(char *dst, const char *src) { __builtin_memcpy(dst, src, 4); }
static long distance(char *b, char *e) { return e - b; }
static void copyOut(char *dst, char *src) { __builtin_memcpy(dst, src, 4); }
long useDistance(char *b, char *e) {
  copyOut(b, e);
  return distance(b, e);
}

// CIRGen emits nothing for `if (0)`, but the call there is still checked.
// NEG-NOT: of 'deadWrite'
static int deadWrite(Node *p) {
  if (0)
    writer(p);
  return p->value;
}
int useDeadWrite(Node *p) { return deadWrite(p); }

// A qualified inner pointer still takes const after its own '*'.
// FIXED: {{^}}static int firstRestrict(const char *const __restrict *argv) { return argv[0][0]; }
static int firstRestrict(const char *__restrict *argv) { return argv[0][0]; }
int useRestrict(const char *__restrict *argv) { return firstRestrict(argv); }

// A function template is proven by every instantiation; one instantiation
// that writes keeps it as it is. Callers need no edit.
// CHECK-DAG: warning: pointer parameter 'p' of 'tmplRead'
// NEG-NOT: of 'tmplWrite'
// NEG-NOT: of 'tmplSpecialized'
// FIXED: {{^}}template <typename T> static T tmplRead(const T *p) { return *p; }
template <typename T> static T tmplRead(T *p) { return *p; }
template <typename T> static void tmplWrite(T *p) { *p = T(); }
template <typename T> static T tmplSpecialized(T *p) { return *p; }
template <> int tmplSpecialized<int>(int *p) { return *p; }
struct Wr { int v; };
int useTmpl(int *i, Node *n, Wr *w) {
  tmplWrite(w);
  tmplSpecialized(n);
  return tmplRead(i) + tmplRead(n).value;
}

// An explicit instantiation repeats the signature; a use from another
// template's instantiation may take the address.
// NEG-NOT: of 'tmplExplicit'
// NEG-NOT: of 'tmplInner'
template <typename T> static T tmplExplicit(T *p) { return *p; }
template int tmplExplicit<int>(int *);
template <typename T> static T tmplInner(T *p) { return *p; }
template <typename T> static T tmplOuter(T *p) {
  T (*f)(T *) = &tmplInner<T>;
  return f(p);
}
int useTmpl2(int *i) { return tmplExplicit(i) + tmplOuter(i); }

// A pointer named by a typedef is spelled out; a const one is left alone.
typedef Node *NodePtr;
// FIXED: {{^}}static int viaTypedef(const Node *p) { return p->value; }
static int viaTypedef(NodePtr p) { return p->value; }
// NEG-NOT: of 'constTypedef'
static int constTypedef(const NodePtr p) { return p->value; }
int useTypedef(Node *n) { return viaTypedef(n) + constTypedef(n); }

// A member of a class template is proven by every instantiation of it, and
// one that is called from another member's instantiation still counts.
// CHECK-DAG: warning: pointer parameter 'p' of 'peek'
// NEG-NOT: of 'poke'
// FIXED: {{^}}  static T peek(const T *p) { return *p; }
namespace {
template <typename T> struct Box2 {
  static T peek(T *p) { return *p; }
  static void poke(T *p) { *p = T(); }
  static T both(T *p) { poke(p); return peek(p); }
};
} // namespace
int useBox2(int *i, Wr *w) { return Box2<int>::both(i) + Box2<Wr>::peek(w).v; }

// Functions stored in a callback table typed by a function-pointer typedef:
// the typedef's parameter and every function stored there change together;
// one that writes keeps them all.
// FIXED: {{^}}typedef int (*ReadFn)(const Node *n);
// FIXED: {{^}}static int readA(const Node *n) { return n->value; }
// FIXED: {{^}}static int readB(const Node *n) { return n->value + 1; }
typedef int (*ReadFn)(Node *n);
static int readA(Node *n) { return n->value; }
static int readB(Node *n) { return n->value + 1; }
struct Table { ReadFn fn; };
static Table tableA = {readA};
int useTable(Node *n) {
  ReadFn f = readB;
  return tableA.fn(n) + f(n);
}
typedef void (*TouchFn)(Node *n);
// NEG-NOT: of 'touchRead'
static void touchRead(Node *n) { (void)n->value; }
static void touchWrite(Node *n) { n->value = 0; }
static TouchFn touches[] = {touchRead, touchWrite};
void useTouch(Node *n) { touches[0](n); touches[1](n); }

// A slot initialized through a const aggregate, as C code writes it.
typedef int (*PeekFn)(Node *n);
struct PeekDesc { PeekFn fn; int flags; };
static int peekWrite(Node *n) { n->value = 1; return 0; }
// NEG-NOT: of 'peekRead'
static int peekRead(Node *n) { return n->value; }
int usePeek(Node *n) {
  const PeekDesc a = {peekWrite, 0};
  const PeekDesc b = {peekRead, 0};
  return a.fn(n) + b.fn(n);
}
