// Evidence and the all-returns-nonnull summary must come from converged
// dataflow states. A loop body is first visited before its back-edge taint
// arrives, when a pointer still looks non-null; reporting from that visit
// emitted nonnull evidence as well as nullable evidence for the same use.
// loop_return_only returns p on every path, so it must have nullable return
// evidence only and no all-returns-nonnull summary.
//
// RUN: rm -f %t.json
// RUN: %clang_cc1 -fsyntax-only -std=c++17 %s \
// RUN:   --ssaf-extract-summaries=NullabilitySafety \
// RUN:   --ssaf-compilation-unit-id=tu --ssaf-tu-summary-file=%t.json
// RUN: %python %S/Inputs/decode-summary.py %t.json | FileCheck %s

int *loop_return(int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    if (i == 5)
      return p;
    p = (int *_Nullable)0;
  }
  return a;
}

int *loop_return_only(int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    if (i == 5)
      return p;
    p = (int *_Nullable)0;
  }
  return p;
}

void sink(int *q);

void loop_argument(int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    sink(p);
    p = (int *_Nullable)0;
  }
}

struct S {
  int *f;
};

void loop_member(S &s, int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    s.f = p;
    p = (int *_Nullable)0;
  }
}

// CHECK:      c:@F@loop_argument#*I#I# NullableEvidence c:@F@sink#*I# param 1
// CHECK-NEXT: c:@F@loop_member#&$@S@S#*I#I# NullableEvidence c:@S@S@FI@f
// CHECK-NEXT: c:@F@loop_return#*I#I# NonnullEvidence c:@F@loop_return#*I#I# return
// CHECK-NEXT: c:@F@loop_return#*I#I# NullableEvidence c:@F@loop_return#*I#I# return
// CHECK-NEXT: c:@F@loop_return_only#*I#I# NullableEvidence c:@F@loop_return_only#*I#I# return
// CHECK-NOT:  {{.}}
