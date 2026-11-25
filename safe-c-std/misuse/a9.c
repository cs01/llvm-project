#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static int g(int argc,char*__null_terminated _Static const*__counted_by(argc) a,char*__null_terminated _Static const*__counted_by(argc) b){ a=b; argc=argc; b=b; return a[0]!=0; }
int main(int argc,char**argv){ return g(argc,argv,argv); }
