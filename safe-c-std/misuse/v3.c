#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct ints v={0}; ints_add(&v,1); struct ints w=v; ints_free(&v); ints_free(&w); return 0; }
