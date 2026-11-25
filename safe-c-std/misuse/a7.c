#include "args.h"
#define VEC_NAME ints
#define VEC_TYPE int
#include "vec.h"
#define MAP_NAME cnt
#define MAP_VALUE int
#include "map.h"
int main(int argc,char**argv){ char *_Owned _Nullable __null_terminated o=strdup("abc"); if(!o) return 1; argv[0]=o; free(o); return args_find(argc,argv,"abc"); }
