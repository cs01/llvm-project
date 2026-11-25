#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(int argc,char**argv){ char *_Owned _Nullable __null_terminated o=strdup("abc"); if(!o) return 1; argv[0]=o; free(o); return args_find(argc,argv,"abc"); }
