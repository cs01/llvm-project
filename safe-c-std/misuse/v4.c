#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct ints v={0}; ints_add(&v,1); v.len=999; int o; int r=ints_pop(&v,&o); ints_free(&v); return r; }
