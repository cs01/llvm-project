// A callee may store one borrowed argument where another points (`*out = in`).
// The caller did not know, so freeing in and then using *out compiled
// (2026-11-24). Now what an out argument reaches borrows the others.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=good -DGOOD %s
// good-no-diagnostics
typedef unsigned long size_t;
void *_Nullable malloc(size_t n) __attribute__((ownership_returns(malloc)));
void free(void *_Nullable p) __attribute__((ownership_takes(malloc, 1)));
struct _View box { int *_Nullable p; };
void set(int *_Nullable __single *out, int *in) { *out = in; }
void set_box(struct box *_Exclusive out, struct box in) { *out = in; }
void look(int *_Nullable __single *out, int *in _Noescape) { *out = 0; *in = 1; }

struct own { int *_Owned _Nullable p; };
static _Thread_local struct own g;
void set_field(int *_Nullable __single *out, struct own *s) { *out = s->p; }

#ifdef GOOD
int field_ok(struct own *_Exclusive s) {
  int *_Nullable __single o = 0;
  set_field(&o, s);
  int r = o ? *o : 0;
  free(_Take(s->p));
  return r;
}
int use_first(void) {
  int *_Nullable buf = malloc(sizeof(int));
  if (!buf) return 0;
  int *_Nullable __single o = 0;
  set(&o, buf);
  int r = o ? *o : 0;
  free(buf);
  return r;
}
int not_kept(void) {
  int *_Nullable buf = malloc(sizeof(int));
  if (!buf) return 0;
  int *_Nullable __single o = 0;
  look(&o, buf);
  free(buf);
  return o ? *o : 0;
}
#else
int ptr(void) {
  int *_Nullable buf = malloc(sizeof(int)); // expected-error {{does not live long enough}}
  if (!buf) return 0;
  int *_Nullable __single o = 0;
  set(&o, buf);       // expected-note 0+ {{aliases}}
  free(buf);          // expected-note {{freed here}}
  return o ? *o : 0;  // expected-note {{later used here}}
}
int view(void) {
  int *_Nullable buf = malloc(sizeof(int)); // expected-error {{does not live long enough}}
  if (!buf) return 0;
  struct box v = {buf}, o = {0}; // expected-note 0+ {{aliases}}
  set_box(&o, v);       // expected-note 0+ {{aliases}}
  free(buf);            // expected-note {{freed here}}
  return o.p ? *o.p : 0; // expected-note {{later used here}}
}
int fnptr(void (*f)(int *_Nullable __single *out, int *in)) {
  int *_Nullable buf = malloc(sizeof(int)); // expected-error {{does not live long enough}}
  if (!buf) return 0;
  int *_Nullable __single o = 0;
  f(&o, buf);         // expected-note 0+ {{aliases}}
  free(buf);          // expected-note {{freed here}}
  return o ? *o : 0;  // expected-note {{later used here}}
}
int field(struct own *_Exclusive s) {
  int *_Nullable __single o = 0;
  set_field(&o, s);  // expected-error {{does not live long enough}}
  free(_Take(s->p)); // expected-note {{destroyed here}}
  return o ? *o : 0; // expected-note {{later used here}}
}
void set_global(int *_Nullable __single *out) { *out = g.p; } // expected-error {{this borrows memory a global owns, and storing it where 'out' points}}
struct S { int x; };
void kept(int *_Nullable __single *out, int *in _Noescape) { *out = in; } // expected-error {{'in' is '_Noescape', so callers count on it not being kept, but it is stored where 'out' points}}
void kept_field(int *_Nullable __single *out, struct S *in _Noescape) { *out = &in->x; } // expected-error {{'in' is '_Noescape'}}
void kept_view(struct box *out, int *in _Noescape) { struct box v = {in}; *out = v; } // expected-error {{'in' is '_Noescape'}}
void kept_by_call(int *_Nullable __single *out, int *in _Noescape) { set(out, in); } // expected-error {{'in' is '_Noescape'}}
#endif
