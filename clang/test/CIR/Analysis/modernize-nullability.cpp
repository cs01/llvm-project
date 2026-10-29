// The opt-in nullability rewrites spell CIR's facts as _Nonnull (dereferenced
// on every path) or _Nullable (checked against null before every use). Under
// -pedantic every qualifier warns, so then neither is proposed.

// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=nonnull,nullable -fdiagnostics-parseable-fixits %s -o /dev/null 2>&1 | FileCheck %s
// RUN: %clang_cc1 -std=c++17 -triple x86_64-unknown-linux-gnu -fclangir -emit-cir -fcir-modernize -fcir-modernize-only=nonnull,nullable -pedantic %s -o /dev/null 2>&1 | FileCheck --check-prefix=PEDANTIC --allow-empty %s

struct N { int v; };

// CHECK: :[[@LINE+2]]:21: warning: pointer parameter 'p' of 'deref' can say whether it may be null
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:21-[[@LINE+1]]:21}:"_Nonnull "
static int deref(N *p) { return p->v; }
// CHECK: :[[@LINE+2]]:23: warning: pointer parameter 'p' of 'checked' can say whether it may be null
// CHECK: fix-it:{{.*}}:{[[@LINE+1]]:23-[[@LINE+1]]:23}:"_Nullable "
static int checked(N *p) { return p ? p->v : 0; }

int use(N *a, N *b) { return deref(a) + checked(b); }

// PEDANTIC-NOT: can say whether it may be null
