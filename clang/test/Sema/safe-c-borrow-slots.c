// A pointer to a place that holds a borrow (`int **`, `struct view *`) is
// only usable where the place is named: kept in a local or chosen by an
// expression, a store through it changed what some place borrows unseen
// (28 accepted use after frees, Lean out-argument work 2026-11-24).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
struct _View box { int *_Nullable p; };
void set(int *_Nullable __single *out, int *in);
void setbox(struct box *out, struct box in);
struct box *self(struct box *b _Lifetimebound);
struct _View holds { int *_Nullable __single *out; }; // expected-error {{field 'out' points to a place}}
void two(int *_Nullable __single *__single *w, int *in);
void deeper(int *_Nullable __single *out, int *in) { two(&out, in); } // expected-error {{which place cannot be told}}

int bad(int c, int *in, struct box v) {
  int *_Nullable __single o = 0, *_Nullable __single o2 = 0;
  int *_Nullable __single *__single p = &o; // expected-error {{local 'p' points to a place}}
  *p = in;
  set(c ? &o : &o2, in); // expected-error {{which place cannot be told}}
  set(((void)c, &o), in); // expected-error {{which place cannot be told}}
  struct box b = {0};
  setbox(self(&b), v);   // expected-error {{which place cannot be told}}
  self(&b)->p = in;      // expected-error {{which place cannot be told}} expected-error {{writing a borrow into a '_View'}}
  return 0;
}
// Fine: the place is named at each use.
void pass(int *_Nullable __single *out, int *in) { set(out, in); *out = in; }
int good(int *in, struct box v) {
  int *_Nullable __single o = 0;
  int *_Nullable __single arr[2] = {0, 0};
  set(&o, in);
  set(&arr[1], in);
  struct box b = {0};
  setbox(&b, v);
  (void)self(&b);
  return o ? *o : 0;
}
