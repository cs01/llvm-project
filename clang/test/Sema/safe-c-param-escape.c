// Fuzzing hole 03 (2026-11-23): what a pointer parameter points at outlives
// the call, so a borrow stored there (`*out = v`) must too. Storing another
// parameter's data, or the parameter's own, is fine. The control run turns
// Safe C off (safec.h's annotations expand to nothing).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

struct _View span { const int *__counted_by(n) p; int n; };

void via_local(int **out) {
  int a[4] = {0};
  int *p = a; // expected-error {{a borrow of local variable 'a' is stored where 'out' points, which the caller can still read after local variable 'a' is gone; store something the caller lends or owns (a field of 'out', or another parameter's data), or return the value instead}}
  *out = p;
} // expected-note {{it ends here}}

void view(struct span *out) {
  int a[4] = {1, 2, 3, 4};
  struct span s = {a, 4}; // expected-error {{a borrow of local variable 'a' is stored where 'out' points}}
  *out = s;
} // expected-note {{it ends here}}

void copy(struct span *out, struct span src) { *out = src; }
void field(const int **out, struct span src) { *out = src.p; }
void own(struct span *rest, struct span *out) { *out = *rest; }
