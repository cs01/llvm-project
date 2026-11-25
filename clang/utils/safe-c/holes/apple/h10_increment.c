#include <ptrcheck.h>
#include <stdlib.h>
static int f(int *__counted_by(n) a, int n, int k) { a[k]++; return a[0]; }
int main(int c, char **v) { int *p = calloc(4, sizeof(int)); if (!p) return 9; return f(p, 4, c + 200); }
