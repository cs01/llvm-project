// RUN: rm -rf %t && mkdir %t && cp %s %t/input.cpp
// RUN: clang-cir-modernize %t/input.cpp --export-fixes=%t/fixes.yaml -- -std=c++17 2>&1 | FileCheck --check-prefix=WARN %s
// RUN: FileCheck --check-prefix=YAML --input-file=%t/fixes.yaml %s
// RUN: clang-cir-modernize %t/input.cpp --fix -- -std=c++17
// RUN: FileCheck --check-prefix=FIXED --input-file=%t/input.cpp %s
// RUN: %clang_cc1 -std=c++17 -fsyntax-only -Wno-unused-function %t/input.cpp

struct Node {
  int value;
};

static int read(Node *n) { return n->value; }

int use(Node *n) {
  int x = 0;
  auto f = [=] { return x; };
  return read(n) + f();
}

// WARN: warning: pointer parameter 'n' of 'read' is dereferenced on every path
// WARN: warning: pointer parameter 'n' of 'read' is never written through
// WARN-NOT: deprecated

// YAML: Replacements:
// YAML: ReplacementText: 'const '
// YAML: ReplacementText: '&'
// YAML: ReplacementText: .
// YAML: ReplacementText: '*'

// FIXED: {{^}}static int read(const Node &n) { return n.value; }
// FIXED: {{^}}  return read(*n) + f();
