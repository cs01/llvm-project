// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fnullability-safety -fnullability-safety-cir %s -o /dev/null 2>&1 | FileCheck %s

struct Node {
  int value;
  Node *next;
};

int afterFor(Node *_Nullable p) {
  for (int i = 0; i < 3; i++) {
  }
  // CHECK: nullability-after-loop.cpp:[[@LINE+1]]:{{[0-9]+}}: warning: dereference of nullable pointer 'p'
  return p->value;
}

int afterWhile(Node *_Nullable p, int k) {
  while (k)
    k--;
  // CHECK: nullability-after-loop.cpp:[[@LINE+1]]:{{[0-9]+}}: warning: dereference of nullable pointer 'p'
  return p->value;
}

int afterDoWhile(Node *_Nullable p, int k) {
  do
    k--;
  while (k);
  // CHECK: nullability-after-loop.cpp:[[@LINE+1]]:{{[0-9]+}}: warning: dereference of nullable pointer 'p'
  return p->value;
}
