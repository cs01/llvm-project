#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// w05 loop through char* param writing bytes over an owner
struct B{int *_Owned _Nullable p;};
static void fill(char *__counted_by(n) d,size_t n){ for(size_t i=0;i<n;i++) d[i]=0x41; }
int main(void){ struct B a={0}; fill((char*)&a,sizeof a); return a.p?*a.p:0; }
