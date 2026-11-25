#include <stdio.h>
#include <stdlib.h>
static int set(const char*k,const char*v){ __unsafe { return setenv(k,v,1); } }
int main(void){ set("A","b"); const char*_Nullable __null_terminated e=getenv("A"); if(e) puts(e); }
