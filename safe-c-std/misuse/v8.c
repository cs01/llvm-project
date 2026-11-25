#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
#define VEC_NAME ptrs
#define VEC_TYPE int *
#include "vec.h"
int main(void){ struct ptrs v={0}; { int x=1; ptrs_add(&v,&x); } int *_Nullable *_Nullable p=ptrs_at(&v,0); return p&&*p?**p:0; }
