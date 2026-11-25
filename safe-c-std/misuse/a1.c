#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
static const char *__null_terminated _Nullable g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ return a[0]; }
int main(int argc,char**argv){ char b[4]="abc"; char *__null_terminated _Static x[1]={0}; return g(1,x)!=0; }
