// An owner is freed, returned, or handed on before it ends; '(void)_Take(p)'
// lets one go on purpose. A pointer tested null holds nothing.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
#include <stdlib.h>
#include <string.h>
void forgot(void) {
  int *_Owned _Nullable p = malloc(4);
} // expected-error {{'p' still owns memory when the function returns}}
void early(int c) {
  int *_Owned _Nullable p = malloc(4);
  if (c)
    return; // expected-error {{'p' still owns memory when the function returns}}
  free(p);
}
void overwritten(void) {
  int *_Owned _Nullable p = malloc(4);
  p = malloc(8); // expected-error {{'p' still owns memory when it is given another value}}
  free(p);
}
int *_Owned _Nullable make(void);
void ignored(void) {
  make();
} // expected-error {{this call's result owns memory that is never freed}}
void one_path(int c) {
  int *_Owned _Nullable p = malloc(4);
  if (c)
    free(p);
} // expected-error {{'p' may still own memory (it is not freed on every path) when the function returns}}
void in_a_loop(void) {
  for (int i = 0; i < 3; i++) {
    int *_Owned _Nullable p = malloc(4); // expected-error {{'p' may still own memory (it is not freed on every path) when its block ends}}
    if (p)
      *p = i;
  }
}
void param(int *_Owned p) {
  (void)p;
} // expected-error {{'p' still owns memory when the function returns}}

int ok_null(void) { int *_Owned _Nullable p = malloc(4); if (!p) return 1; free(p); return 0; }
int ok_eq(void) { int *_Owned _Nullable p = malloc(4); if (p == NULL) return 1; free(p); return 0; }
void ok_if(void) { int *_Owned _Nullable p = malloc(4); if (p) free(p); }
int *_Owned _Nullable ok_return(void) { int *_Owned _Nullable p = malloc(4); return p; }
void ok_null_first(void) { int *_Owned _Nullable p = NULL; p = malloc(4); free(p); }
void ok_loop(void) { for (int i = 0; i < 3; i++) { int *_Owned _Nullable p = malloc(4); free(p); } }
void ok_param(int *_Owned p) { free(p); }
void ok_nested(void) { free(make()); }
struct n { int *_Owned _Nullable p; };
void ok_stored(struct n *x) { int *_Owned _Nullable p = malloc(4); free(_Take(x->p)); x->p = p; }
void ok_two(void) {
  int *_Owned _Nullable p = malloc(4);
  if (!p) return;
  int *_Owned _Nullable q = malloc(4);
  if (!q) { free(p); return; }
  free(q);
  free(p);
}
int ok_forget(int *_Owned _Nullable p, int c) {
  if (c) {
    (void)_Take(p); // let go on purpose
    return 1;
  }
  free(p);
  return 0;
}
int kept_take(int *_Owned _Nullable p) {
  int *_Owned _Nullable q = _Take(p);
  return 0; // expected-error {{'q' still owns memory when the function returns}}
}
