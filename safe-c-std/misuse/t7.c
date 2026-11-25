#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(int c,char**v){ struct cnt m={0}; cnt_put(&m,STR("a"),1); int *_Nullable p=c>5?cnt_get(&m,STR("a")):cnt_get(&m,STR("b")); cnt_free(&m); return p?*p:0; }
