#include <stdio.h>
int mid(unsigned lo, unsigned hi) { return lo + (hi - lo) / 2; }
int main(void) { printf("mid(10,20) = %u\n", mid(10, 20)); return 0; }
