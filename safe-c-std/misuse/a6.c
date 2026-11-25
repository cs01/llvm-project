#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(int argc,char**argv){ char *__null_terminated l[2]={"x","y"}; return args_find(2,l,"y"); }
