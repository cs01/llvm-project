#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
static int g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ return a[0][0]; }
static int h(int argc,char*__null_terminated*__counted_by(argc) a){ return g(argc,a); }
int main(int argc,char**argv){ return h(argc,argv); }
