// Memory starts zeroed, so a non-null field, element or global may hold null
// until it is set. Loading one traps, also when it is only passed on or called
// (blind attack cause 27, 2026-11-24).
// RUN: %clang_cc1 -triple arm64-apple-macosx -fsafe-c -fbounds-safety \
// RUN:   -fnullability-safety -fnullability-default=nonnull -emit-llvm -O0 \
// RUN:   -include safec.h %s -o - | FileCheck %s
struct s { const char *_Static p; const char *_Static _Nullable q; int (*f)(void); };
void take(const char *p);
// CHECK-LABEL: define {{.*}} @pass(
// CHECK: [[P:%.*]] = load ptr, ptr {{.*}}
// CHECK: icmp ne ptr {{.*}}, null
// CHECK: call void @llvm.ubsantrap
void pass(struct s *x) { take(x->p); }
// CHECK-LABEL: define {{.*}} @call(
// CHECK: icmp ne ptr {{.*}}, null
// CHECK: call void @llvm.ubsantrap
int call(struct s *x) { return x->f(); }
// A local was set before use, and a nullable is checked by the analysis.
// CHECK-LABEL: define {{.*}} @local(
// CHECK-NOT: ubsantrap
// CHECK: ret void
void local(const char *p) { take(p); }
