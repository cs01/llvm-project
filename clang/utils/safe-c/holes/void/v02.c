#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v02 launder bytes via void* var into memcpy dest holding pointer (hole 13)
struct B{int *_Owned _Nullable p;};
int main(void){ struct B s={0}; long bytes[1]={0x4141414141}; void *v=&s; memcpy(v,bytes,sizeof s); return s.p?*s.p:0; }
