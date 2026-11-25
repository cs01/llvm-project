// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ if(c>50) goto *&&a; goto *(void*)0; a: puts("a"); return 0; }
