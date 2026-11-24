// Under -fsafe-c a function marked alloc_size counts as a fresh allocation
// (its void * result may be given a type), and its body is checked to return
// only fresh memory or null.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void *memset(void *, int, size_t);

static int counter;
struct node { int v; struct node *_Nullable next; }; // expected-error {{holds a pointer nothing tracks}}

__attribute__((alloc_size(1))) void *_Nullable my_alloc(size_t n) {
  void *_Nullable p = malloc(n);
  if (!p)
    return 0;
  counter++;
  return p;
}

__attribute__((alloc_size(1))) void *_Nullable wraps(size_t n) {
  return my_alloc(n);
}

static long pool[64];
__attribute__((alloc_size(1))) void *_Nullable lies(size_t n) {
  (void)n;
  return pool; // expected-error {{'lies' is marked 'alloc_size', so callers give its result a type as fresh memory, but this returns something else}}
}

_Returns_owned struct node *_Nullable make(void) {
  struct node *_Nullable n = wraps(sizeof(struct node));
  return n;
}
