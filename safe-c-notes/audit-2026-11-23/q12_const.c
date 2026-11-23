#include <stdio.h>
static const int k = 5;
int main(void) { int *p = (int *)&k; *p = 6; printf("%d\n", k); return 0; }
