// The last causes from the Lean out-argument work (2026-11-24); every program
// is also in clang/utils/safe-c/holes.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void *_Nullable realloc(void *_Nullable p, size_t n);
void free(void *_Nullable);
struct own { int *_Owned _Nullable p; };
struct _View box { int *_Nullable p; };
struct _View of_owner { struct own *s; }; // expected-error {{'_View' field 's' points to a struct that owns memory}}
static _Thread_local struct own g;
void shared(struct own *s);
void clean(struct own *s) { if (s->p) *s->p = 1; }

void owned_stored(int *_Nullable __single *out, int *_Owned q) { // expected-error {{'q' is owned by this function}}
  *out = q;
  free(q);
}
void promise(int *_Nullable __single *out, int *in _Noescape);
void promise(int *_Nullable __single *out, int *in) { *out = in; } // expected-error {{'in' is '_Noescape'}}
void choose(int c, struct own *a, struct own *b) {
  shared(c ? a : b); // expected-error {{which place cannot be told}}
}
void lend_global(void) {
  shared(&g); // expected-error {{global 'g' owns memory, and this call is passed a pointer to it}}
  clean(&g);  // names no global: fine
  struct own l = {_Take(g.p)};
  shared(&l);
  g.p = _Take(l.p);
}
static int *_Nullable get(void) { return g.p; }
int through_pointer(void) {
  int *_Nullable (*fp)(void) = get;
  int *_Nullable q = fp(); // expected-error {{does not live long enough}}
  free(_Take(g.p));        // expected-note {{destroyed here}}
  return q ? *q : 0;       // expected-note {{later used here}}
}
