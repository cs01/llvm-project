// Under -fsafe-c a function that frees a parameter (directly, or by passing it
// to another function that takes it) must say so with ownership_takes: the
// lifetime analysis trusts that annotation at every call.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void free(void *_Nullable);
void *_Nullable realloc(void *_Nullable, size_t) __attribute__((alloc_size(2)));
struct node { int v; };

void node_free(struct node *_Nullable n) {
  free(n); // expected-error {{'node_free' frees its parameter 'n', but callers are not told and may use it afterwards; declare it '__attribute__((ownership_takes(malloc, 1)))' (safec.h: '__safec_takes(1)')}}
}

__safec_takes(1) void node_free_ok(struct node *_Nullable n) { free(n); }

void wrapper(struct node *_Nullable a, struct node *_Nullable b) {
  node_free_ok(b); // expected-error {{'wrapper' frees its parameter 'b'}}
  (void)a;
}

__safec_takes(2) void wrapper_ok(struct node *_Nullable a,
                                 struct node *_Nullable b) {
  node_free_ok(b);
  (void)a;
}

// Bounds safety wraps an allocator's result; the call inside is still seen.
void *_Nullable grow(void *_Nullable p, size_t n) {
  return realloc(p, n); // expected-error {{'grow' frees its parameter 'p'}}
}
