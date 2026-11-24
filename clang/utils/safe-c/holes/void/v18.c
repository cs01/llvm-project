#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v18 void* return helper re-typed
struct B{int *_Owned _Nullable p;};
static void *id(void *p _Lifetimebound){ return p; }
int main(void){ long x=0x4141414141; struct B *b=id(&x); return b->p?*b->p:0; }
