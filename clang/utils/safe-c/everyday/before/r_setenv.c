// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(void){ setenv("A","b",1); const char*e=getenv("A"); if(e) puts(e); }
