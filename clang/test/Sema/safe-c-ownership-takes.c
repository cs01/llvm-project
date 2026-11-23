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
__safec_returns_owned struct node *_Nullable malloc_node(void);

void node_free(struct node *_Nullable n) {
  free(n); // expected-error {{'node_free' frees its parameter 'n', but callers are not told and may use it afterwards; mark the parameter '__safec_owned'}}
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
__safec_returns_owned void *_Nullable grow(void *_Nullable p, size_t n) {
  return realloc(p, n); // expected-error {{'grow' frees its parameter 'p'}}
}

// The parameter form: no index to keep in sync.
void node_free_param(struct node *_Nullable __safec_owned n) { free(n); }
int use_after_param_free(void) {
  struct node *_Nullable n = malloc_node(); // expected-error {{does not live long enough}}
  node_free_param(n);                       // expected-note {{freed here}}
  return n ? n->v : 0;                      // expected-note {{later used here}}
}
