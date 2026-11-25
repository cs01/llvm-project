#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct ints v={0}; ints_add(&v,1); int *_Nullable p=ints_at(&v,0); ints_free(&v); return p?*p:0; }
