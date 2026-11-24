#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v10 owned void holding struct with owners freed shallow then inner used
struct O{int *_Owned _Nullable p;};
int main(void){ struct O *_Owned _Nullable o=malloc(sizeof *o); if(!o) return 0; o->p=malloc(4); int*_Nullable q=o->p; void *_Nullable v=_Take(o); free(v); if(q) return *q; return 0; }
