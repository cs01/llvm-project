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

// The note carries a fix-it when a count-like parameter sits next to the pointer.
// RUN: not %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -fdiagnostics-parseable-fixits %s 2>&1 | FileCheck %s
typedef __SIZE_TYPE__ size_t;
void fill(int *buf, size_t len) {
  for (size_t i = 0; i < len; i++)
    buf[i] = 0; // expected-error {{array subscript on single pointer 'buf' must use a constant index of 0}} \
                // expected-note {{in Safe C an unannotated pointer points to exactly one element}}
}
// CHECK: fix-it:"{{.*}}":{[[@LINE-5]]:16-[[@LINE-5]]:16}:"__counted_by(len) "

// No fix-it for 'size': it may count bytes.
void bytes(int *buf, size_t size) {
  buf[1] = 0; // expected-error {{array subscript on single pointer 'buf'}} \
              // expected-note {{in Safe C an unannotated pointer points to exactly one element}}
}
// CHECK-NOT: fix-it:"{{.*}}":{[[@LINE-5]]

// main's argv: the fix-it counts it by argc.
int main(int argc, char **argv) {
  return argv[1] != 0; // expected-error {{array subscript on single pointer 'argv'}} \
                       // expected-note {{in Safe C an unannotated pointer points to exactly one element}}
}
// CHECK: fix-it:"{{.*}}":{[[@LINE-4]]:27-[[@LINE-4]]:27}:"__counted_by(argc) "
