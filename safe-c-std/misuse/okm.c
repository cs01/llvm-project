#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct cnt m={0}; if(!cnt_put(&m,STR("a"),1)) return 1; int *_Nullable n=cnt_get(&m,STR("a")); if(n) (*n)++; int *_Nullable k=cnt_get(&m,STR("a")); int r=k?*k:0; cnt_free(&m); return r==2?0:1; }
