#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct cnt m={0}; if(!cnt_put(&m,STR("a"),1)) return 1; int *_Nullable n=cnt_get(&m,STR("a")); if(n) (*n)++; int *_Nullable k=cnt_get(&m,STR("a")); int r=k?*k:0; cnt_free(&m); return r==2?0:1; }
