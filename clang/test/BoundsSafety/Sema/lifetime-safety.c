// Lifetime safety sees through -fbounds-safety's AST (opaque values,
// materialized sequences, pointer promotion, bounds-representation casts),
// and recognizes the C allocator by name when builtins are off.
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -Wlifetime-safety-use-after-free \
// RUN:   -Wlifetime-safety-return-stack-addr -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -fno-builtin \
// RUN:   -Wlifetime-safety-use-after-free -Wlifetime-safety-return-stack-addr -verify %s
// RUN: %clang_cc1 -fsyntax-only -fno-builtin -Wlifetime-safety-use-after-free \
// RUN:   -Wlifetime-safety-return-stack-addr -verify %s

typedef __SIZE_TYPE__ size_t;
void *malloc(size_t) __attribute__((alloc_size(1)));
void free(void *);

int uaf(void) {
  int *p = malloc(sizeof(int)); // expected-warning {{object allocated by 'malloc' does not live long enough}}
  free(p);                      // expected-note {{freed here}}
  return *p;                    // expected-note {{later used here}}
}

int *escape(void) {
  int x = 1;
  int *p = &x; // expected-warning {{stack memory associated with local variable 'x' is returned}}
  return p;    // expected-note {{returned here}}
}
