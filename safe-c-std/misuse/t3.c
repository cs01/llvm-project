#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); int *_Nullable p=cnt_get(&m,STR("a")); cnt_free(&m); return p?*p:0; }
