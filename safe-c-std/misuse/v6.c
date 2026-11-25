#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(void){ int *_Nullable p=NULL; { struct ints v={0}; ints_add(&v,1); p=ints_at(&v,0); } return p?*p:0; }
