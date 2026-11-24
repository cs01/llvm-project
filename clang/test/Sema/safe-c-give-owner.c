// Memory given away (free, an owned parameter, field or return) must be a
// whole block this code owns. Through a local pointer nothing checked that:
// freeing an array, a borrow, a global or the middle of a block compiled
// (found probing alloca, 2026-11-24).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void *_Nullable realloc(void *_Nullable p, size_t n);
void free(void *_Nullable);
struct O { char *_Owned _Nullable p; };
void take(char *_Owned p);
static _Thread_local int g[4];

void array(void) { char a[8] = {0}; char *p = a; free(p); } // expected-error {{must be a whole block}} expected-note {{this value}}
void middle(void) { char *_Nullable p = malloc(8); if (!p) return; char *q = p + 1; free(q); } // expected-error {{must be a whole block}} expected-note {{this value}}
void advanced(void) { char *_Nullable p = malloc(8); if (!p) return; p++; free(p); } // expected-error {{must be a whole block}} expected-note {{this value}}
void global(void) { int *p = g; free(p); } // expected-error {{must be a whole block}} expected-note {{this value}}
void borrowed(int *p) { int *q = p; free(q); } // expected-error {{must be a whole block}} expected-note {{this value}}
void field(struct O *o) { char *_Nullable q = o->p; free(q); } // expected-error {{must be a whole block}} expected-note {{this value}}
void stack(void) { char *b = __builtin_alloca(8); free(b); } // expected-error {{must be a whole block}} expected-note {{this value}}
void into_field(struct O *_Exclusive o) { char a[4] = {0}; char *p = a; o->p = p; } // expected-error {{must be a whole block}} expected-note {{this value}}
void to_param(void) { char a[4] = {0}; char *p = a; take(p); } // expected-error {{must be a whole block}} expected-note {{this value}}
_Returns_owned char *_Nullable ret(void) { char *_Nullable p = malloc(8); if (!p) return 0; return p + 1; } // expected-error {{must be a whole block}}
void one_path(int c, char *b) { char *_Nullable p = malloc(8); if (c) p = b; free(p); } // expected-error {{must be a whole block}} expected-note {{this value}}

void good(struct O *_Exclusive o, int c) {
  char *_Nullable p = malloc(8);
  char *_Nullable q = p;
  if (c) q = realloc(q, 16);
  free(q);
  char *_Nullable t = _Take(o->p);
  free(t);
  o->p = malloc(4);
  char *_Nullable r = 0;
  if (c) r = malloc(1);
  free(r);
}
_Returns_owned char *_Nullable good_ret(void) { char *_Nullable p = malloc(8); return p; }
