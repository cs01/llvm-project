#include <stdio.h>
#include <stdlib.h>
int main(int c,char**v){ char*__null_terminated _Nullable end=NULL; long x=strtol("12ab",&end,10); if(end) printf("%ld %s\n",x,end); }
