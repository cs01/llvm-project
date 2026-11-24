#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v06 double free via void*
int main(void){ int *_Nullable b=malloc(4); void *_Nullable v=b; free(b); free(v); return 0; }
