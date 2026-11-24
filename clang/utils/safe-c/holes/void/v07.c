#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v07 void* in struct field freed
struct H{void *_Owned _Nullable p;};
int main(void){ struct H h={malloc(4)}; void *_Nullable q=h.p; free(_Take(h.p)); if(q) memset(q,1,4); return 0; }
