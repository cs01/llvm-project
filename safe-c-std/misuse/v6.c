#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(void){ int *_Nullable p=NULL; { struct ints v={0}; ints_add(&v,1); p=ints_at(&v,0); } return p?*p:0; }
