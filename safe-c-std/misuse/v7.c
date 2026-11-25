#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ struct ints v={0}; ints_add(&v,1); ints_add(&v,*(ints_at(&v,0)?:&(int){3})); ints_free(&v); return 0; }
