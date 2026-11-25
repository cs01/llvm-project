#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
struct bx { int v[4]; }; static int *_Nullable in(struct bx *b){ return &b->v[1]; }
int main(void){ int *_Nullable p=NULL; { p=in(&(struct bx){{1,2,3,4}}); } struct bx c={{9,9,9,9}}; return p?*p+c.v[0]:0; }
