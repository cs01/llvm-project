#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// w06 void* then char*
struct B{int *_Owned _Nullable p;};
int main(void){ struct B a={0}; void *v=&a; char *c=v; c[0]=0x41; c[4]=0x41; return a.p?*a.p:0; }
