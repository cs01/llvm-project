#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// w02 hole 13 with a view
struct _View B{int*_Nullable p;};
int main(void){ struct B s={0}; long bytes[2]={0x4141414141,0x4141414141}; void *v=&s; memcpy(v,bytes,sizeof s); return s.p?*s.p:0; }
