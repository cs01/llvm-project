#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v15 void** 
int main(void){ int *_Nullable __single b=0; void *_Nullable __single *vp=(void*_Nullable __single*)&b; static long x; *vp=&x; return 0; }
