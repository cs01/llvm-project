// M2 rule 11: a _View struct holds borrows, tracked like a pointer: building
// it from pointers, or writing a field by name, adds their loans; reading a
// field borrows what the view does. So a view outliving a local array or a
// freed allocation is an error, and a function returning a view must say
// which parameter it borrows from. (Fields share one set of loans, which is
// stricter than one root per field.) The control run is plain bounds safety,
// which runs no lifetime analysis.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -mllvm -safe-c-ownership -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct _View span { const int *_Nullable __counted_by_or_null(n) p; int n; };

int scope_escape(void) {
  struct span s = {0, 0};
  {
    int x[4] = {1, 2, 3, 4};
    s.p = x; // expected-error {{local variable 'x' does not live long enough}}
    s.n = 4;
  } // expected-note {{destroyed here}}
  return s.p ? s.p[0] : 0; // expected-note {{later used here}}
}

int after_free(void) {
  int *_Nullable a = malloc(16); // expected-error {{object allocated by 'malloc' does not live long enough}}
  if (!a)
    return 0;
  struct span s = {a, 4}; // expected-note {{aliases}}
  free(a); // expected-note {{freed here}}
  return s.p ? s.p[0] : 0; // expected-note {{later used here}}
}

int fine(const int *__counted_by(4) a) {
  struct span s = {a, 4};
  return s.p ? s.p[1] : 0;
}

struct span first_two(const int *__counted_by(n) a, int n) { // expected-error {{'first_two' returns a pointer borrowed from 'a'}}
  struct span s = {a, n < 2 ? n : 2};
  return s; // expected-note {{param returned here}}
}
struct span first_two_ok(const int *__counted_by(n) a _Lifetimebound, int n) {
  struct span s = {a, n < 2 ? n : 2};
  return s;
}
