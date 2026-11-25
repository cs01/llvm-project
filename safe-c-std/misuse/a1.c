#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
static const char *__null_terminated _Nullable g(int argc,char*__null_terminated _Static const*__counted_by(argc) a){ return a[0]; }
int main(int argc,char**argv){ char b[4]="abc"; char *__null_terminated _Static x[1]={0}; return g(1,x)!=0; }
