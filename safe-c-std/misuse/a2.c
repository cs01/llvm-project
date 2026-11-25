#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static int g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ return a[0][0]; }
static int h(int argc,char*__null_terminated*__counted_by(argc) a){ return g(argc,a); }
int main(int argc,char**argv){ return h(argc,argv); }
