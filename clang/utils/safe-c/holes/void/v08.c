#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v08 bounds through void*
int main(void){ char a[4]={0}; void *v=a; memset(v,1,8); return a[0]; }
