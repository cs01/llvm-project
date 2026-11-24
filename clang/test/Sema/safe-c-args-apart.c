// Arguments of one call that overlap (Lean out-argument work, 2026-11-24): the
// callee may free or replace what it is lent exclusively or given, then use
// another argument that borrows it. 12 such use after frees were accepted.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct own { int *_Owned _Nullable p; };
struct _View box { int *_Nullable p; };
void ex(int *_Nullable in, struct own *_Exclusive s);
void exview(struct box q, struct own *_Exclusive s);
void shared(struct own *a, struct own *_Exclusive s);
void given(int *in, int *_Owned b);
void unchecked(int *_Nullable _Unchecked in, struct own *_Exclusive s);
int *_Nullable get(struct own *s _Lifetimebound);

void bad(struct own *_Exclusive s, struct own *_Exclusive t) {
  int *_Nullable q = s->p;
  ex(q, s);                // expected-error {{is lent exclusively to this call}}
  struct box v = {t->p};   // expected-error {{borrow}}
  exview(v, t);            // expected-note 2 {{}}
}
void bad_shared(struct own *_Exclusive s) {
  struct own *a = s;
  shared(a, s); // expected-error {{is lent exclusively to this call}}
}
void bad_getter(struct own *_Exclusive s) {
  ex(get(s), s); // expected-error {{borrow}} expected-note 2 {{}}
}
void bad_given(void) {
  int *_Nullable b = malloc(4);
  if (!b) return;
  int *q = b;
  given(q, b);  // expected-error {{is lent exclusively to this call}}
}
void good(struct own *_Exclusive s, struct own *_Exclusive t, int *in) {
  ex(t->p, s);
  ex(in, s);
  struct box v = {in};
  exview(v, s);
  shared(t, s);
  unchecked(s->p, t);
  int *_Nullable q = s->p;
  if (q) *q = 1;
  ex(0, s);
}
