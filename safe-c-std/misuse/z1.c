#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
struct in { int *_Owned _Nullable p; long a[6]; }; struct out { int k; struct in i; long b; };
int main(void){ struct out x={0}; x.i.p=calloc(1,4); int *_Nullable q=x.i.p; free(_Take(x.i.p)); x=(struct out){0}; return q?*q:0; }
