// RUN: rm -rf %t && split-file %s %t && cp %t/input.cpp %t/orig.cpp
// RUN: clang-cir-modernize %t/input.cpp --export-fixes=%t/fixes.yaml -- -std=c++17 2>&1 | FileCheck --check-prefix=WARN %s
// RUN: FileCheck --check-prefix=YAML --input-file=%t/fixes.yaml %s

// The proof refuses every edit when every edited version changes the IR.
// RUN: clang-cir-modernize %t/input.cpp --fix --break-proof-for-testing -- -std=c++17 2>&1 | FileCheck --check-prefix=BROKEN %s
// RUN: diff %t/orig.cpp %t/input.cpp

// -Werror turns the rewrites' warnings into errors; their edits still count.
// RUN: clang-cir-modernize %t/input.cpp --fix -- -std=c++17 -Werror 2>&1 | FileCheck --check-prefix=SUMMARY %s
// RUN: FileCheck --check-prefix=FIXED --input-file=%t/input.cpp %s
// RUN: %clang_cc1 -std=c++17 -fsyntax-only -Wno-unused-function %t/input.cpp

//--- node.h
struct Node {
  int value;
};

// Single-file mode leaves a function declared in a header alone.
int peek(Node *n);

//--- input.cpp
#include "node.h"

int peek(Node *n) { return n->value; }

static int read(Node *n) { return n->value; }

int use(Node *n) {
  int x = 0;
  auto f = [=] { return x; };
  return read(n) + peek(n) + f();
}

// WARN: warning: pointer parameter 'n' of 'read' is dereferenced on every path
// WARN: warning: pointer parameter 'n' of 'read' is never written through
// WARN-NOT: peek
// WARN-NOT: deprecated

// YAML: Replacements:
// YAML: FilePath: '{{.*}}input.cpp'
// YAML-NEXT: Offset:
// YAML-NEXT: Length:
// YAML-NEXT: ReplacementText: 'const '

// BROKEN: input.cpp: 0 edits applied, {{[0-9]+}} left out: they change the IR

// SUMMARY: input.cpp: {{[0-9]+}} edits applied{{$}}

// FIXED: {{^}}static int read(const Node &n) { return n.value; }
// FIXED: {{^}}  return read(*n) + peek(n) + f();

