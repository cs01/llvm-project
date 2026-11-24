#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v04 memset through void*
struct B{int *_Owned _Nullable p;};
int main(void){ struct B s={0}; void *v=&s; memset(v,0x41,sizeof s); return s.p?*s.p:0; }
