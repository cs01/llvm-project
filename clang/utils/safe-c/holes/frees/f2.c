#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
int main(void){ char *_Nullable p=malloc(8); if(!p) return 0; char *q=p+1; free(q); return 0; }
