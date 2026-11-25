#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static int one(int*_Nullable p){ return p?*p:0; }
int main(void){ struct ints v={0}; ints_add(&v,1); for(int i=0;i<99;i++) ints_add(&v,one(ints_at(&v,0))); ints_free(&v); return 0; }
