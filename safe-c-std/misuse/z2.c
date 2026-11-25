#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
struct in { int *_Owned _Nullable p; long a[6]; }; struct out { int k; struct in i; long b; };
int main(void){ int *_Owned _Nullable g=NULL; for(int t=0;t<2;t++){ struct out x={0}; if(t) { int *_Nullable q=x.i.p; if(q) return *q; } x.i.p=calloc(1,4); free(x.i.p); } free(g); return 0; }
