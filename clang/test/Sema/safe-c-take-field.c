// Found porting inih (2026-11-24): a value moved out of an owned field with
// _Take carried no loan (fields and globals hold none of their block), so a
// second free, or a use after the free, compiled. The taken value now gets a
// fresh owner loan, like an allocation result. One loan stood for every block
// a call site returns, so in a loop freeing the last iteration's block ended
// this one's; earlier iterations now share a separate loan.
// RUN: %clang_cc1 -fsafe-c -fsyntax-only -verify %s
// RUN: %clang_cc1 -fsafe-c -fsyntax-only -verify=good -DGOOD %s
// good-no-diagnostics
#include <safec.h>
void free(void *_Nullable p) __attribute__((ownership_takes(malloc, 1)));
struct B { char *_Owned _Nullable p; };
static _Thread_local char *_Owned _Nullable gp;

#ifdef GOOD
void once(struct B *_Exclusive b) { char *_Nullable old = _Take(b->p); free(old); }
void once_global(void) { char *_Nullable old = _Take(gp); free(old); }
void *_Nullable malloc(unsigned long n) __attribute__((ownership_returns(malloc)));
struct N { struct N *_Owned _Nullable next; };
void delete_list(struct N *_Owned _Nullable n) {
  struct N *_Owned _Nullable next = 0;
  while (n) { next = _Take(n->next); free(n); n = next; }
}
void keep_last(int k) {
  char *_Nullable prev = 0;
  for (int i = 0; i < k; i++) { char *_Nullable b = malloc(8); free(prev); prev = b; }
  free(prev);
}
#else
void twice(struct B *_Exclusive b) {
  char *_Nullable old = _Take(b->p); // expected-error {{object taken from 'b->p' does not live long enough}}
  free(old);                         // expected-note {{freed here}}
  free(old);                         // expected-note {{later used here}}
}
char use_after(struct B *_Exclusive b) {
  char *_Nullable old = _Take(b->p); // expected-error {{object taken from 'b->p' does not live long enough}}
  free(old);                         // expected-note {{freed here}}
  return old ? old[0] : 0;           // expected-note {{later used here}}
}
void twice_global(void) {
  char *_Nullable old = _Take(gp); // expected-error {{object taken from 'gp' does not live long enough}}
  free(old);                       // expected-note {{freed here}}
  free(old);                       // expected-note {{later used here}}
}
void *_Nullable malloc(unsigned long n) __attribute__((ownership_returns(malloc)));
void freed_in_loop(int k) {
  char *_Nullable prev = 0;
  for (int i = 0; i < k; i++) {
    char *_Nullable b = malloc(8); // expected-error {{object allocated by 'malloc' does not live long enough}}
    free(b);                       // expected-note {{freed here}}
    prev = b;                      // expected-note {{aliases}}
  }
  free(prev);                      // expected-note {{later used here}}
}
#endif
