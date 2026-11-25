#include <stdio.h>
#include <string.h>
static void f(size_t n, char *__counted_by(n) p) { sscanf("6", "%zu", &n); memset(p, 0, n); }
int main(void) { char a[4]; f(4, a); return 0; }
