// A getter of a global's owned pointer, called from another file: the caller
// sees only the declaration, so `p = get(); drop(); *p` compiled (2026-11-24).
// Rejected at the definition unless the function is static (its callers see
// the body, and the borrow ends with the next call that may free it).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
void free(void *_Nullable p) __attribute__((ownership_takes(malloc, 1)));
static _Thread_local struct { int *_Owned _Nullable p; int n; } g;
static _Thread_local int plain;
int *_Nullable get(void) { return g.p; } // expected-error {{this returns a borrow of memory a global owns}}
int *_Nullable get_local(void) { int *_Nullable r = g.p; return r; } // expected-error {{this returns a borrow of memory a global owns}}
static int *_Nullable get_static(void) { return g.p; }
int *_Nullable relay(void) { return get_static(); } // expected-error {{this returns a borrow of memory a global owns}}
// Fine: not owned memory, so nothing frees it.
int *get_plain(void) { return &plain; }
int *get_count(void) { return &g.n; }
int read(void) { int *_Nullable p = get_static(); return p ? *p : 0; }
int dropped(void) {
  int *_Nullable p = get_static(); // expected-error {{does not live long enough}}
  free(_Take(g.p));                // expected-note {{destroyed here}}
  return p ? *p : 0;               // expected-note {{later used here}}
}
