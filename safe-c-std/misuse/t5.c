#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
static int *_Owned _Nullable mk(struct str s){ int*_Owned _Nullable p=malloc(4); if(p)*p=(int)s.len; return p; }
int main(void){ int *_Owned _Nullable p=mk(STR("abc")); int*_Nullable q=p; free(p); free(mk(STR("q"))); return q?*q:0; }
