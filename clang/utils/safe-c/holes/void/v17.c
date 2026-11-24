#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v17 void* from malloc given two types
struct B{int *_Owned _Nullable p;};
int main(void){ void *_Nullable v=malloc(16); long *_Nullable l=v; struct B *_Nullable b=v; if(!l||!b) return 0; l[0]=0x4141414141; return b->p?*b->p:0; }
