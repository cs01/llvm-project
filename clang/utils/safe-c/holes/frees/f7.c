#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
struct O{char *_Owned _Nullable p;};
int main(void){ struct O o={0}; char *_Nullable p=malloc(8); if(!p) return 0; p+=2; o.p=p; free(_Take(o.p)); return 0; }
