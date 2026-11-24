#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
_Returns_owned static char *_Nullable mk(void){ char *_Nullable p=malloc(8); if(!p) return 0; return p+1; }
int main(void){ free(mk()); return 0; }
