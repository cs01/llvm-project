// A Safe C error must not stop the null analysis from reporting later ones.
// The first error below comes from Sema while parsing; the rest come from the
// end-of-TU null analysis, which used to be skipped once any error occurred.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s

#define NULL ((void *)0)
void take(int *p);

int first(void) {
  int *_Nonnull q = NULL; // expected-error {{initializing a variable of nonnull type}}
  return *q;              // expected-error {{dereference of nullable pointer}} expected-note {{add a null check}}
}

int later_deref(void) {
  int *q = NULL;
  return *q; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check}}
}

void later_arg(void) {
  int *q = NULL;
  take(q); // expected-error {{passing nullable pointer to nonnull parameter 'p'}} expected-note {{add a null check}}
}

int later_join(int k) {
  int x = 1;
  int *q = NULL;
  if (k)
    q = &x;
  return *q; // expected-error {{dereference of nullable pointer}} expected-note {{add a null check}}
}
