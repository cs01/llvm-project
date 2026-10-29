// Project mode: each translation unit exports its candidates, edits and
// blockers as JSON, and, given a plan, the edits its own declarations and
// calls need for functions defined elsewhere.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-export=a.json a.cpp -o /dev/null
// RUN: FileCheck --check-prefix=PROPOSE --input-file=a.json %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-plan=plan.json -fcir-modernize-export=b.json b.cpp -o /dev/null
// RUN: FileCheck --check-prefix=CALLER --input-file=b.json %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-plan=plan.json -fcir-modernize-export=c.json c.cpp -o /dev/null
// RUN: FileCheck --check-prefix=BLOCK --input-file=c.json %s
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-plan=missing.json -fcir-modernize-export=d.json b.cpp -o /dev/null 2>&1 | FileCheck --check-prefix=NOPLAN %s

// The defining TU proposes an externally visible function and edits its
// header declaration too.
// PROPOSE:      "usr": "c:@F@first#*$@S@Node#",
// PROPOSE-NEXT: "index": 0,
// PROPOSE-NEXT: "kind": "ref",
// PROPOSE-NEXT: "function": "first",
// PROPOSE-NEXT: "param": "head",
// PROPOSE-NEXT: "defined": true,
// PROPOSE:      "file": "{{.*}}node.h",
// PROPOSE-NEXT: "offset": 55,
// PROPOSE-NEXT: "length": 1,
// PROPOSE-NEXT: "text": "&"

// A caller edits the header declaration and its call sites.
// CALLER:      "function": "first",
// CALLER-NEXT: "param": "head",
// CALLER-NEXT: "defined": false,
// CALLER:      "file": "{{.*}}node.h",
// CALLER-NEXT: "offset": 55,
// CALLER:      "file": "{{.*}}b.cpp",
// CALLER-NEXT: "offset": 70,
// CALLER-NEXT: "length": 0,
// CALLER-NEXT: "text": "*"
// CALLER:      "file": "{{.*}}b.cpp",
// CALLER-NEXT: "offset": 81,
// CALLER-NEXT: "length": 1,
// CALLER-NEXT: "text": ""

// A TU that takes the address blocks it.
// BLOCK:      "function": "first",
// BLOCK:      "defined": false,
// BLOCK:      "blocked": "address taken"

// NOPLAN: error: cannot read modernize plan missing.json

// CIRGen cannot handle d.cpp (a block pointer), but it still exports its
// declarations and calls from the AST, without facts.
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fblocks -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-plan=plan.json -fcir-modernize-export=d.json d.cpp -o /dev/null
// RUN: FileCheck --check-prefix=ASTONLY --input-file=d.json %s
// ASTONLY:      "facts": false,
// ASTONLY:      "function": "first",
// ASTONLY:      "defined": false,
// ASTONLY:      "file": "{{.*}}d.cpp",
// ASTONLY-NEXT: "offset": {{[0-9]+}},
// ASTONLY-NEXT: "length": 1,
// ASTONLY-NEXT: "text": ""

// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref,bogus a.cpp -o /dev/null 2>&1 | FileCheck --check-prefix=UNKNOWN %s
// UNKNOWN: error: unknown modernize rewrite 'bogus'

//--- node.h
struct Node { int value; Node *next; };
int first(Node *head);
//--- a.cpp
#include "node.h"
int first(Node *head) { return head->value; }
//--- b.cpp
#include "node.h"
int use(Node *n) { Node l{1, nullptr}; return first(n) + first(&l); }
//--- c.cpp
#include "node.h"
int (*keep)(Node *) = first;
//--- d.cpp
#include "node.h"
void (^block)(void);
int viaBlock() { Node l{1, nullptr}; return first(&l); }
//--- plan.json
{"functions": [{"usr": "c:@F@first#*$@S@Node#", "index": 0, "kind": "ref"}]}
