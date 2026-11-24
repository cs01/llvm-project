#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
static void f(char *N *out){ char *b=alloca(8); b[0]=1; *out=b; }
int main(void){ char *N o=0; f(&o); return o?*o:0; }
