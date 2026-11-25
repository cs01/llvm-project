#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
struct in { int *p; long a[6]; }; struct out { int k; struct in i; long b; };
int main(void){ struct out x={0}; return *x.i.p; }
