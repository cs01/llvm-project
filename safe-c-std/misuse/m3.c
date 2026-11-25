#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ struct cnt m={0}; cnt_put(&m,STR("a"),1); if(m.p){ m.p[0].at=99999; m.p[0].len=99999; for(size_t i=0;i<m.cap;i++){ m.p[i].at=99999; m.p[i].len=9999; } } int *_Nullable p=cnt_get(&m,STR("a")); int r=p?*p:0; cnt_free(&m); return r; }
