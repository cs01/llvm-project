#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); m.len=0; m.filled=0; for(int i=0;i<3;i++) cnt_put(&m,STR("q"),i); text_free(&m.keys); int *_Nullable p=cnt_get(&m,STR("a")); int r=p?*p:0; cnt_free(&m); return r; }
