#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
VEC(ptrs, int *);
int main(void){ struct ptrs v={0}; { int x=1; ptrs_add(&v,&x); } int *_Nullable *_Nullable p=ptrs_at(&v,0); return p&&*p?**p:0; }
