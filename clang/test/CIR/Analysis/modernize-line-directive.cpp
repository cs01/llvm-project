// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s

struct Node {
  int value;
};

static int before(Node *n) { return n->value; }

#line 100 "generated.y"
static int after(Node *n) { return n->value; }

int use(Node *n) { return before(n) + after(n); }

// CHECK: warning: pointer parameter 'n' of 'before'
// CHECK-NOT: of 'after'
// CHECK-NOT: generated.y
