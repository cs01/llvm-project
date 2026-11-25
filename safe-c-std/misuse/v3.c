#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct ints v={0}; ints_add(&v,1); struct ints w=v; ints_free(&v); ints_free(&w); return 0; }
