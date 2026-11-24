// M2: pointer kinds are part of the type, so they can sit on an inner
// pointer (a vector of owned strings), in either order with _Nullable, and
// survive bounds-safety's type rebuilding. Heap memory that holds pointers
// must give them a kind. Plain clang sees no kinds (control run).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);

struct strvec {
  char *__safec_owned _Nullable *_Nullable __safec_owned __counted_by_or_null(len) items;
  size_t len;
};
struct either { int *_Nullable __safec_owned a; int *__safec_owned _Nullable b; };
struct ptrs { int *_Nullable *_Nullable __safec_owned slots; }; // expected-error {{this heap memory holds pointers of type 'int *__single _Nullable', which nothing tracks, so they can outlive what they point to; give the inner pointer a kind, as in 'T *__safec_owned *' if the memory owns what they point to, or 'T *__safec_static *'}}

void f(int *p) {
  int *_Nullable *_Nullable h = malloc(sizeof(int *)); // expected-error {{this heap memory holds pointers of type 'int *__single _Nullable'}}
  int *__safec_owned _Nullable *_Nullable ok = malloc(sizeof(int *));
  int *_Nullable local[2] = {p, p}; // a stack array of borrows is fine
  (void)h; (void)ok; (void)local;
}
