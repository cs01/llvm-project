#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v09 callback context of the wrong type
struct C{int *_Owned _Nullable p;};
static void cb(void *u){ struct C *c=(struct C*)u; if(c->p) *c->p=1; }
static void run(void(*f)(void*),void*u){ f(u);}
int main(void){ long x[1]={0x4141414141}; run(cb,x); return 0; }
