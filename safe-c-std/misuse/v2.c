#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct ints v={0}; ints_add(&v,1); int *_Nullable p=ints_at(&v,0); ints_free(&v); return p?*p:0; }
