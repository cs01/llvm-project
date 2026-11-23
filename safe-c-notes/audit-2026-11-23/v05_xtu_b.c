#include <stdio.h>
extern int *shared;
int main(void) { printf("%p\n", (void *)shared); *shared = 1; return 0; }
