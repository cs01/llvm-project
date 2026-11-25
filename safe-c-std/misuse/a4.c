#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static int g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ return args_find(argc+1,a,"zz"); }
int main(int argc,char**argv){ return g(argc,argv); }
