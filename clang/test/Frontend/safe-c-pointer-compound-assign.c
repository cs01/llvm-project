// 'p += q' with two pointers is invalid C. bounds-safety cast p, still a
// glvalue in a compound assignment, as if it were a value, which hit an
// llvm_unreachable before the error could be reported.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
int *_Nullable g(void);
int h(void) {
  int a[2] = {0};
  int *p = a;
  p += g(); // expected-error-re {{invalid operands to binary expression ('int *__bidi_indexable{{.*}}' and 'int *__single{{.*}}')}}
  return 0;
}
