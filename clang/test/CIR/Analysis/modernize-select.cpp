// -fcir-modernize-only= and -fcir-modernize-skip= take rewrite names or the
// groups identical and refinement. Without -only, the default rewrites run:
// nonnull and nullable only when asked for.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize %s -o /dev/null 2>&1 | FileCheck --check-prefixes=REF,CONST,NONULL %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-skip=ref %s -o /dev/null 2>&1 | FileCheck --check-prefixes=NOREF,CONST,NONULL %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=identical -fcir-modernize-skip=const %s -o /dev/null 2>&1 | FileCheck --check-prefixes=REF,NOCONST,NULL %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-skip=identical %s -o /dev/null 2>&1 | FileCheck --check-prefix=NONE --allow-empty %s
// RUN: not %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-skip=bogus %s -o /dev/null 2>&1 | FileCheck --check-prefix=BAD %s

struct N { int v; };
static int get(N *p) { return p->v; }
int use(N *q) { return get(q); }

// REF:     'p' of 'get' is dereferenced on every path
// NOREF-NOT: dereferenced on every path
// CONST:   'p' of 'get' is never written through
// NOCONST-NOT: never written through
// NULL:    'p' of 'get' can say whether it may be null
// NONULL-NOT: can say whether it may be null
// NONE-NOT: warning
// BAD:     error: unknown modernize rewrite 'bogus'
