// Safe C errors say why an unannotated pointer is rejected and what to write.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s

void second(int *p) {
  p[1] = 0; // expected-error {{array subscript on single pointer 'p' must use a constant index of 0}} \
            // expected-note {{in Safe C an unannotated pointer points to exactly one element; if 'p' points to an array, declare its length with '__counted_by(n)'}}
}

void arith(int *p) { // expected-note {{pointer 'p' declared here}}
  p++; // expected-error {{pointer arithmetic on single pointer 'p' is out of bounds; consider adding '__counted_by' to 'p'}}
}
