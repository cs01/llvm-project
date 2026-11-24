#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v03 memcpy through void* param
struct B{int *_Owned _Nullable p;};
static void cp(void *__sized_by(n) d,const void *__sized_by(n) s,size_t n){ memcpy(d,s,n);}
int main(void){ struct B s={0}; long bytes[1]={0x4141414141}; cp(&s,bytes,sizeof s); return s.p?*s.p:0; }
