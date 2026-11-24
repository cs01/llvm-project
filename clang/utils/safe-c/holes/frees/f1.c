#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
int main(void){ char a[8]={0}; char *p=a; free(p); return 0; }
