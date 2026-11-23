#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct S { int *_Nullable p; };
int test_a(void) { int *_Nullable arr[1] = {0}; { int x = 1; arr[0] = &x; } int *_Nullable q = arr[0]; return q ? *q : 0; }
int test_b(void) { struct S s = {0}; { int x = 1; s.p = &x; } int *_Nullable q = s.p; return q ? *q : 0; }
int test_d(int c) { int *_Nullable p = malloc(4); if (!p) return 0; if (c) free(p); return *p; }
int test_f(void) { int *__single _Nullable p = malloc(4); if (!p) return 0; int *__single _Nullable *pp = &p; free(*pp); return *p; }
int test_g(void) { struct S s; s.p = malloc(4); struct S t = s; free(s.p); int *_Nullable q = t.p; return q ? *q : 0; }
int test_i(int c) { int *_Nullable p = malloc(4); int *_Nullable q = malloc(4); if (!p || !q) return 0; int *r = c ? p : q; free(p); return *r; }
int test_k(void) { int *__single _Nullable p = malloc(4); int *__single _Nullable q = 0; if (!p) return 0; memcpy(&q, &p, sizeof p); free(p); return q ? *q : 0; }
int test_l(void) { int *_Nullable p = malloc(4); if (!p) return 0; int *q = p; for (int i = 0; i < 2; i++) { *q = i; free(p); } return 0; }
int test_m(void) { int *_Nullable p = malloc(4); if (!p) return 0; int **_Nullable h = malloc(sizeof(int *)); if (!h) return 0; *h = p; free(p); return **h; }
int main(void) { return 0; }
