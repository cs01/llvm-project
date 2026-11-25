// Blind attack, group 1 (2026-11-24): rows of two-dimensional arrays, strdup,
// compiler builtins, scanf and printf sizes, library handles, owned elements.
// REQUIRES: system-darwin
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -mllvm -safe-c-ownership=false -DRUN -O1 %s -o %t && %t
// RUN: not --crash %t 2
// RUN: not --crash %t 2 3
// RUN: not --crash %t 2 3 4
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int rows(int (*__counted_by(n) p)[3], int n, int i) { return p[i][0]; }
static int local(int i) { int l[4][8] = {0}; l[i][0] = 1; return l[0][0]; }
static int vla(int n, int i) { int v[n][3]; for (int k = 0; k < n; k++) v[k][0] = 1; return v[i][0]; }
#ifndef RUN
int dup(void) {
  char *_Nullable p = strdup("abc");
  if (!p) return 1;
  free(p);
  return p[0];
}
void builtins(void) {
  char a[4];
  __builtin_strcpy(a, "abcdefgh"); // expected-error {{is a compiler builtin that accesses memory without any check}} expected-warning 0+ {{}} expected-error 0+ {{}}
}
void scans(const char *s, char *_Nullable np) {
  char b[4], c;
  int i;
  sscanf(s, "%10s", b); // expected-error {{can store 11 characters}} expected-warning 0+ {{}}
  sscanf(s, "%3s %c %d", b, &c, &i);
  sscanf(s, "%d", (int *)&c); // expected-error {{scanf stores a whole value here unchecked}} expected-error 0+ {{}} expected-note 0+ {{}}
}
void stars(const char *__counted_by(n) p, int n) {
  printf("%.*s", n, p); // expected-error {{'%.*s' is told to print}}
}
void handles(void) {
  FILE *_Nullable f = fopen("/dev/null", "r");
  if (!f) return;
  free(f); // expected-error {{is closed, not freed}}
}
#endif
int main(int argc, char **argv) {
  int m[2][3] = {0};
  (void)argv;
  if (argc == 1) return rows(m, 2, 1) + local(3) + vla(2, 1) - 1;
  if (argc == 2) return rows(m, 2, 900);
  if (argc == 3) return local(900);
  return vla(2, 900);
}
