#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static int *_Owned _Nullable mk(struct str s){ int*_Owned _Nullable p=malloc(4); if(p)*p=(int)s.len; return p; }
int main(void){ int *_Owned _Nullable p=mk(STR("abc")); free(p); return p?*p:0; }
