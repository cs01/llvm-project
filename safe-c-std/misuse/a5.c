#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
static int g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ a[0]=a[1]; return 0; }
int main(int argc,char**argv){ return g(argc,argv); }
