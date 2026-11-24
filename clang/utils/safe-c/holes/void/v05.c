#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v05 void* keeps a dangling borrow
int main(void){ int *_Nullable b=malloc(4); if(!b) return 0; void *v=b; free(b); memset(v,0,4); return 0; }
