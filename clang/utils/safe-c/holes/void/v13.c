#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v13 qsort bytes of pointers
struct B{int *_Owned _Nullable p;};
static int cmp(const void*a,const void*b){ (void)a;(void)b; return 0; }
int main(void){ long x[2]={0x41414141,2}; void *v=x; qsort(v,2,sizeof(long),cmp); return 0; }
