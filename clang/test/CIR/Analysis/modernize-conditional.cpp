// Facts and edits hold for one build configuration. A rewrite is refused when
// its function's body or one of its edits is inside an #if region, or when a
// branch this build skips names the function. An include guard is not such
// a region.

// RUN: rm -rf %t && split-file %s %t && cd %t
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=ref -fcir-modernize-export=a.json a.cpp -o /dev/null
// RUN: FileCheck --input-file=a.json %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=lifetimebound -fcir-modernize-export=lb.json a.cpp -o /dev/null
// RUN: FileCheck --check-prefix=ATTR --input-file=lb.json %s

//--- guarded.h
#ifndef GUARDED_H
#define GUARDED_H
struct N { int v; };
inline int inGuard(N *p) { return p->v; }
#endif

//--- a.cpp
#include "guarded.h"

static int plain(N *p) { return p->v; }

static int bodyIf(N *p) {
#ifdef DEBUG
  if (!p)
    return 0;
#endif
  return p->v;
}

#if 1
static int definedIn(N *p) { return p->v; }
#endif

static int calledWhenSkipped(N *p) { return p->v; }
#ifdef NOT_THIS_BUILD
int other(N n) { return calledWhenSkipped(&n); }
#endif

// An attribute changes no type, so a skipped call needs no edit.
static const int &attrWhenSkipped(const N &n) { return n.v; }
#ifdef NOT_THIS_BUILD
int third(N n) { return attrWhenSkipped(n); }
#endif

int use(N *q) {
  return inGuard(q) + plain(q) + bodyIf(q) + definedIn(q) + calledWhenSkipped(q) +
         attrWhenSkipped(*q);
}

// CHECK:      "function": "inGuard",
// CHECK-NOT:  "blocked"
// CHECK:      "edits"
// CHECK:      "function": "plain",
// CHECK-NOT:  "blocked"
// CHECK:      "edits"
// CHECK:      "function": "bodyIf",
// CHECK:      "blocked": "defined in or across an #if region"
// CHECK:      "function": "definedIn",
// CHECK:      "blocked": "defined in or across an #if region"
// CHECK:      "function": "calledWhenSkipped",
// CHECK:      "blocked": "named in an #if branch this build skips"
// CHECK:      "skippedNames": [
// CHECK-DAG:  "calledWhenSkipped"
// CHECK-DAG:  "DEBUG"

// ATTR:      "function": "attrWhenSkipped",
// ATTR:      "changesType": false,
// ATTR-NOT:  "blocked"
// ATTR:      "edits"
