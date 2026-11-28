// _Slice: a buffer's elements from an index, checked once where the slice is
// made, so the loop over it has no checks. A slice that does not fit stops
// the program there.
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -O2 -DRUN %s -o %t && %t 0
// RUN: %clang -fsafe-c -O2 -DRUN %s -o %t && not --crash %t 1
// RUN: %clang -fsafe-c -O2 -DRUN %s -o %t && not --crash %t 2
// RUN: %clang -fsafe-c -O2 -S -emit-llvm %s -o - | FileCheck %s
// expected-no-diagnostics
#include <stdlib.h>

struct _View window {
  unsigned char *__counted_by(len) out;
  size_t len;
};

// Copies n bytes from back bytes before at to at, as a decompressor does.
// CHECK-LABEL: define {{.*}} @copy_back(
// The loop body has no trap: the checks are where the slices are made.
// CHECK: ubsantrap
// CHECK: for.cond.preheader
// CHECK-NOT: ubsantrap
// CHECK: {{^}}}
void copy_back(struct window *w, size_t at, size_t back, size_t n) {
  _Slice(unsigned char, to, w->out, at, n);
  _Slice(const unsigned char, from, w->out, at - back, n);
  for (size_t i = 0; i < to_n; i++)
    to[i] = from[i];
}

#ifdef RUN
int main(int argc, char **argv) {
  (void)argc;
  unsigned char buf[16] = "abcd";
  struct window w = {buf, sizeof buf};
  int which = argv[1][0] - '0';
  if (which == 0)
    copy_back(&w, 4, 4, 8); // fits: abcdabcdabcd
  if (which == 1)
    copy_back(&w, 4, 5, 2); // starts before the buffer
  if (which == 2)
    copy_back(&w, 12, 4, 8); // runs past its end
  return buf[11] == 'd' ? 0 : 1;
}
#endif
