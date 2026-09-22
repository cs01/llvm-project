// Evidence and the all-returns-nonnull summary must come from converged
// dataflow states. A loop body is first visited before its back-edge taint
// arrives, when a pointer still looks non-null; reporting from that visit
// emitted "returns nonnull" and then "returns nullable" for the same return.
//
// RUN: %clang_cc1 -fsyntax-only -fnullability-safety -std=c++17 -Rnullsafe-evidence %s -verify

int *loop_return(int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    if (i == 5)
      return p; // expected-remark-re{{function 'loop_return' of global scope (declared at {{.*}}) returns nullable}}
    p = (int *_Nullable)0;
  }
  return a; // expected-remark-re{{function 'loop_return' of global scope (declared at {{.*}}) returns nonnull}}
}

void sink(int *q);

void loop_argument(int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    sink(p); // expected-remark-re{{parameter 'q' of 'sink' (declared at {{.*}}) called with nullable argument}}
    p = (int *_Nullable)0;
  }
}

struct S {
  int *f;
};

void loop_member(S &s, int *_Nonnull a, int n) {
  int *p = a;
  for (int i = 0; i < n; i++) {
    s.f = p; // expected-remark-re{{member 'f' of S (declared at {{.*}}) assigned from nullable source}}
    p = (int *_Nullable)0;
  }
}
