#include <stdio.h>
long cell = 0x41414141;
extern int *view __asm__("_cell");
int main(void) { *view = 1; return 0; }
