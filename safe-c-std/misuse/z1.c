#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
struct in { int *_Owned _Nullable p; long a[6]; }; struct out { int k; struct in i; long b; };
int main(void){ struct out x={0}; x.i.p=calloc(1,4); int *_Nullable q=x.i.p; free(_Take(x.i.p)); x=(struct out){0}; return q?*q:0; }
