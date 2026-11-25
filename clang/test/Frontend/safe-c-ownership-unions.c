// A tagged union's members, and other annotated fields and variables, follow
// the same ownership rules as any other.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
enum kind { NUM, ADD };
struct e {
  enum kind kind;
  struct e *_Owned _Nullable a;
  union _Tagged_by(kind) {
    double num _When(NUM);
    int op _When(ADD);
  };
};
double eval(const struct e *x) {
  switch (x->kind) {
  case NUM: return x->num;
  case ADD: {
    int op = x->op;
    return (x->a ? eval(x->a) : 0) + op;
  }
  }
  return 0;
}
int freed(void) {
  struct e *_Owned _Nullable x = calloc(1, sizeof *x);
  if (!x) return 1;
  free(x);
  if (x->kind == ADD) return x->op; // expected-error {{'x' is used after it was freed}}
  return 0;
}
const int *_Nullable get(const struct e *x) { return x->kind == ADD ? &x->op : NULL; }
int gone(void) {
  struct e *_Owned _Nullable x = calloc(1, sizeof *x);
  if (!x) return 1;
  const int *_Nullable p = get(x);
  free(x);
  return p ? *p : 0; // expected-error {{'p' is tested after it was freed}}
}
struct noted { int *_Owned _Nullable __attribute__((annotate("x"))) p; };
int annotated(void) {
  struct noted x = {0};
  x.p = calloc(1, sizeof(int));
  int *_Nullable q __attribute__((annotate("y"))) = x.p;
  free(_Take(x.p));
  return q ? *q : 0; // expected-error {{'q' is tested after '_Take'}}
}
