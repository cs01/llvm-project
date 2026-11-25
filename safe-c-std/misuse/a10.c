#include "map.h"
#include "args.h"
VEC(ints,int);
MAP(cnt,int);
int main(int argc,char**argv){ char b[4]="abc"; const char *__null_terminated _Nullable k=NULL; { char *__null_terminated _Owned _Nullable o=strdup("x"); if(!o) return 1; const char *__null_terminated l[1]={o}; int r=args_find(1,l,"y"); free(o); return r; } }
