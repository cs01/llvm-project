#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
static void take(char *_Owned p){ free(p); }
int main(void){ char a[8]={0}; char *p=a; take(p); return 0; }
