#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
int main(void){ char *b=alloca(8); free(b); return 0; }
