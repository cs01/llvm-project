#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(int argc,char**argv){ char *__null_terminated l[2]={"x","y"}; return args_find(2,l,"y"); }
