// 'p += q' with p counted and q not is invalid C; computing the operands'
// common type asserted, because only one side carries the count.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fbounds-safety -include safec.h -fsyntax-only -Xclang -verify %s
struct N { int *__counted_by(n) f; int n; };
int *g(void);
int h(struct N *k) {
  k->f += g(); // expected-error-re {{invalid operands to binary expression ('int *__single __counted_by(n)'{{.*}} and 'int *__single')}}
  return 0;
}
