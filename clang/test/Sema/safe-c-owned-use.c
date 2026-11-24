// A freed pointer stored into an _Owned place is a use after free (Juliet CWE
// 415 variants 34 and 67, 2026-11-24): the callee or the next free got it.
// An owned parameter returned as owned is moved, not borrowed.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t);
void free(void *_Nullable);
typedef struct { char *_Owned _Nullable a; } st;
static _Thread_local char *_Owned _Nullable g;
void use(st *_Exclusive s);
void assigned(st *_Exclusive u) {
  char *_Nullable d = malloc(5); // expected-error {{does not live long enough}}
  free(d);  // expected-note {{freed here}}
  u->a = d; // expected-note {{later used here}}
}
void initialized(void) {
  char *_Nullable d = malloc(5); // expected-error {{does not live long enough}}
  free(d);      // expected-note {{freed here}}
  st u = {d};   // expected-note {{later used here}}
  use(&u);
}
void global(void) {
  char *_Nullable d = malloc(5); // expected-error {{does not live long enough}}
  free(d); // expected-note {{freed here}}
  g = d;   // expected-note {{later used here}}
}
void good(void) {
  st u = {malloc(4)};
  st v = {_Take(u.a)};
  free(_Take(v.a));
  char *_Nullable d = malloc(5);
  u.a = d;
  use(&u);
  free(_Take(u.a));
}
_Returns_owned char *pass(char *_Owned p) { p[0] = 1; return p; }
