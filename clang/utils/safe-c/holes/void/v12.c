#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v12 null survives call (hole 7)
static _Thread_local struct {int *_Owned _Nullable p;} g;
static void clear(void){ free(_Take(g.p)); }
int main(void){ g.p=malloc(4); if(g.p){ clear(); return *g.p; } return 0; }
