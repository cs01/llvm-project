#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// w03 view memcpy through void* param
struct _View B{int*_Nullable p;};
static void cp(void *__sized_by(n) d,const void *__sized_by(n) s,size_t n){ memcpy(d,s,n);}
int main(void){ struct B s={0}; long bytes[2]={0x4141414141,0x4141414141}; cp(&s,bytes,sizeof s); return s.p?*s.p:0; }
