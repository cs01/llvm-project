// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref %s -o /dev/null 2>&1 | FileCheck --check-prefix=NEG %s
// RUN: cp %s %t.cpp
// RUN: %clang_cc1 -std=c++17 -fcxx-exceptions -fexceptions -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-apply %t.cpp -o /dev/null
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
static int initUse(Node *p) {
  int v = p->value;
  return v;
}

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
  total += initUse(n);
  return total;
}

// CHECK-DAG: warning: pointer parameter 'head' of 'first' is dereferenced on every path, so a null argument is already undefined behavior; it can be a reference
// CHECK-DAG: warning: pointer parameter 'p' of 'viaCallee'
// CHECK-DAG: warning: pointer parameter 'p' of 'deref'
// CHECK-DAG: warning: pointer parameter 's' of 'method'
// CHECK-DAG: warning: pointer parameter 'p' of 'initUse'
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

struct WithCtor {
  int v;
  WithCtor(Node *n) : v(n->value) {}
  ~WithCtor() {}
};
template <typename T> static int templated(T *t) { return t->value; }
int useCtor(Node *n) { return WithCtor(n).v + templated(n); }

// Functions reached from a template instantiation are left alone: the edit
// would have to go in the template, which other instantiations share. classof
// is the classic case, called as To::classof(&V) by isa<>. With the converting
// constructor below, a missed call site would still compile, binding the
// reference to a temporary Shape built from the pointer.
namespace {
struct Shape {
  int kind;
  Shape(const Shape *Parent = nullptr) : kind(Parent ? Parent->kind : 0) {}
  static bool classof(const Shape *S) { return S->kind == 1; }
};
template <typename To, typename From> bool isaShape(const From &V) {
  return To::classof(&V);
}
int fromTemplate(Node *p) { return p->value; }
template <typename T> int callsFromTemplate(T, Node *n) {
  return fromTemplate(n);
}
} // namespace
int useTemplates(Node *n) {
  Shape s;
  return isaShape<Shape>(s) + callsFromTemplate(1, n);
}
// NEG-NOT: of 'classof'
// NEG-NOT: of 'fromTemplate'

// A dependent member call can only reach methods, so it does not block a free
// function of the same name; an unqualified dependent call blocks what its
// lookup already found.
namespace {
int print(Node *p) { return p->value; }
template <typename T> int callsMember(T &t) { return t.print(); }
int adlTarget(Node *p) { return p->value; }
template <typename T> int callsUnqualified(T *t) { return adlTarget(t); }
} // namespace
int useDependent(Node *n) { return print(n) + adlTarget(n); }
// CHECK-DAG: warning: pointer parameter 'p' of 'print'
// NEG-NOT: of 'adlTarget'

// __PRETTY_FUNCTION__ spells the parameter types, so the rewrite would change
// the string; __func__ spells only the name.
static int pretty(Node *p) {
  int v = p->value;
  auto f = [] { return __PRETTY_FUNCTION__[0]; };
  return v + f();
}
static int named(Node *p) { return p->value + __func__[0]; }
int useNames(Node *n) { return pretty(n) + named(n); }
// CHECK-DAG: warning: pointer parameter 'p' of 'named'
// NEG-NOT: of 'pretty'

// A logging helper that captures the caller's location the way
// std::source_location::current() does: through a default member initializer
// holding a default argument.
namespace std {
struct source_location {
  struct __impl {
    const char *_M_file_name;
    const char *_M_function_name;
    unsigned _M_line, _M_column;
  };
  static const __impl *current(const __impl *l = __builtin_source_location()) {
    return l;
  }
};
} // namespace std
struct Where { const std::source_location::__impl *loc = std::source_location::current(); };
static int logged(Node *p) {
  int v = p->value;
  Where w{};
  return v + w.loc->_M_function_name[0];
}
int useLogged(Node *n) { return logged(n); }
// NEG-NOT: of 'logged'
