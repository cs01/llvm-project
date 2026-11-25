// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static _Thread_local int*_Owned _Nullable g;
static void drop(void){ free(_Take(g)); }
int main(void){ int*k=malloc(4); if(!k) return 1; g=k; drop(); *k=5; }
