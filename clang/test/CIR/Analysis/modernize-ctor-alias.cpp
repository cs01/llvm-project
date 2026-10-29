// RUN: %clang_cc1 -std=c++17 -triple aarch64-linux-android -mconstructor-aliases -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=identical %s -o /dev/null 2>&1 | FileCheck %s

// Out-of-line complete-object constructors and destructors become bodiless
// aliases of the base-object ones; the analyses must skip them.

struct Node {
  int value;
};

struct Holder {
  Holder(const Node *n);
  ~Holder();
  int v;
};

Holder::Holder(const Node *n) : v(n->value) {}
Holder::~Holder() {}

static int read(Node *n) { return n->value; }

int use(Node *n) { return Holder(n).v + read(n); }

// CHECK: warning: pointer parameter 'n' of 'read'
