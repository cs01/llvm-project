#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
struct bx { int v[4]; }; static int *_Nullable in(struct bx *b){ return &b->v[1]; }
int main(void){ int *_Nullable p=in(&(struct bx){{1,2,3,4}}); return p?*p:0; }
