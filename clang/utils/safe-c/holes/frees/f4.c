#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
static _Thread_local int g[4];
int main(void){ int *p=g; free(p); return 0; }
