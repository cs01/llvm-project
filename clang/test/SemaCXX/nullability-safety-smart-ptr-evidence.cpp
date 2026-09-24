// A smart pointer warns only on evidence that it can be null: an explicit
// _Nullable, a flow fact (default-constructed, reset(), moved-from), or a call
// to a function whose body has a path that returns null. Without evidence it is
// silent under every default, so a getter that never returns null is not
// flagged at every use.
//
// UNSUPPORTED: target={{.*-windows.*}}
// REQUIRES: system-darwin || system-linux
// RUN: %clangxx -fsyntax-only -fnullability-safety -fnullability-default=nullable -std=c++17 %s -Xclang -verify
// RUN: %clangxx -fsyntax-only -fnullability-safety -fnullability-default=nonnull -std=c++17 %s -Xclang -verify

#include <memory>
#include <utility>

struct Node {
  virtual ~Node();
  int value;
  void f();
  std::shared_ptr<Node> child(int);
};

// ---- No evidence: silent ----

std::shared_ptr<Node> opaque();

void unannotated_param(std::unique_ptr<Node> p) { p->f(); }

void unannotated_ref_param(const std::shared_ptr<Node> &p) { p->f(); }

void opaque_call() {
  auto p = opaque();
  p->f();
  opaque()->f();
}

struct Holder {
  std::unique_ptr<Node> m;
  void use() { m->f(); }
};

void member_of_param(Holder &h) { h.m->f(); }

std::shared_ptr<Node> always_make() { return std::make_shared<Node>(); }

std::unique_ptr<Node> always_make_unique(bool b) {
  if (b)
    return std::make_unique<Node>();
  auto p = std::make_unique<Node>();
  return p;
}

void never_null() {
  auto a = always_make();
  a->f();
  always_make_unique(true)->f();
}

// ---- Explicit _Nullable ----

std::shared_ptr<Node> _Nullable find_nullable(int);

void explicit_nullable() {
  auto p = find_nullable(1);
  p->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// ---- The callee has a path that returns null ----

std::shared_ptr<Node> find_child(Node &n, int i) {
  if (i < 0)
    return nullptr;
  return n.child(i);
}

void uses_find_child(Node &n) {
  auto c = find_child(n, 1);
  c->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void checks_find_child(Node &n) {
  if (auto c = find_child(n, 1))
    c->f();
}

void direct_deref(Node &n) {
  find_child(n, 1)->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void assign_from(Node &n) {
  std::shared_ptr<Node> c = std::make_shared<Node>();
  c = find_child(n, 2);
  c->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

std::shared_ptr<Node> default_ctor_return(bool b) {
  if (b)
    return std::make_shared<Node>();
  return std::shared_ptr<Node>();
}

std::shared_ptr<Node> brace_return(bool b) {
  if (b)
    return std::make_shared<Node>();
  return {};
}

std::shared_ptr<Node> local_maybe_set(bool b) {
  std::shared_ptr<Node> r;
  if (b)
    r = std::make_shared<Node>();
  return r;
}

std::shared_ptr<Node> transitive(Node &n) { return find_child(n, 0); }

void null_return_shapes(bool b, Node &n) {
  default_ctor_return(b)->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  brace_return(b)->f();        // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  local_maybe_set(b)->f();     // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
  transitive(n)->f();          // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

struct Registry {
  std::shared_ptr<Node> lookup(int id) {
    if (id == 0)
      return nullptr;
    return std::make_shared<Node>();
  }
};

void method_callee(Registry &r) {
  auto n = r.lookup(3);
  n->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

// ---- Flow evidence ----

void default_constructed() {
  std::unique_ptr<Node> p;
  p->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void after_reset(std::unique_ptr<Node> p) {
  p.reset();
  p->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void after_move(std::unique_ptr<Node> p) {
  auto q = std::move(p);
  q->f();
  p->f(); // expected-warning {{dereference of nullable pointer}} expected-note {{add a null check}}
}
