// The opt-in nullability rewrites spell CIR's facts as _Nonnull (dereferenced
// on every path) or _Nullable (checked against null before every use), through
// the project's own macro for each: the qualifiers are clang's alone, and the
// macro is how a project keeps them from other compilers. Without such a
// macro, or under -pedantic (every qualifier warns), neither is proposed.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=nonnull,nullable -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=nonnull,nullable -pedantic %s -o /dev/null 2>&1 | FileCheck --check-prefix=REFUSED --allow-empty %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=nonnull,nullable -DNO_MACRO %s -o /dev/null 2>&1 | FileCheck --check-prefix=REFUSED --allow-empty %s

#ifndef NO_MACRO
#define MY_NONNULL _Nonnull
#define MY_NULLABLE _Nullable
int already(int *MY_NONNULL p, int *MY_NULLABLE q);
#endif

struct N { int v; };

// CHECK: :[[@LINE+2]]:21: warning: pointer parameter 'p' of 'deref' can say whether it may be null
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:21-[[@LINE+1]]:21}:"MY_NONNULL "
static int deref(N *p) { return p->v; }
// CHECK: :[[@LINE+2]]:23: warning: pointer parameter 'p' of 'checked' can say whether it may be null
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:23-[[@LINE+1]]:23}:"MY_NULLABLE "
static int checked(N *p) { return p ? p->v : 0; }

int use(N *a, N *b) { return deref(a) + checked(b); }

// REFUSED-NOT: can say whether it may be null
