// Under -fsafe-c, attributes, builtins, pragmas and asm that can switch off
// Safe C's checks are errors outside __unsafe.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=plain %s
// plain-no-diagnostics

__attribute__((no_sanitize("undefined"))) int nosan(int a, int b) { return a / b; } // expected-error {{'no_sanitize' can switch off Safe C's checks}}

__unsafe __attribute__((no_sanitize("undefined"))) int nosan_ok(int a, int b) { return a / b; }

int uninit(void) {
  __attribute__((uninitialized)) int a[4]; // expected-error {{'uninitialized' can switch off Safe C's checks}}
  a[0] = 1;
  return a[0];
}

int assume(int *__counted_by(2) a, int i) {
  __builtin_assume(i >= 0 && i < 2); // checked: traps when false
  return a[i];
}

int unreach(int i) {
  if (i > 3)
    __builtin_unreachable(); // checked: traps when reached
  return i;
}

void asm_use(void) {
  __asm__ volatile("nop"); // expected-error {{inline assembly can switch off Safe C's checks}}
}

struct node { int v; };
int raw(int *__unsafe_indexable p, struct node *__unsafe_indexable n) {
  int a = *p;     // expected-error {{dereferencing an '__unsafe_indexable' pointer can switch off Safe C's checks}}
  int b = p[1];   // expected-error {{indexing an '__unsafe_indexable' pointer can switch off Safe C's checks}}
  int c = n->v;   // expected-error {{'->' on an '__unsafe_indexable' pointer can switch off Safe C's checks}}
  return a + b + c;
}

int raw_ok(int *__unsafe_indexable p) {
  __unsafe { return *p; }
}
