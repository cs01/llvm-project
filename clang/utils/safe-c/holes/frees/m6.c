#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
struct O{char *_Owned _Nullable p;};
static void f(struct O *_Exclusive o){ char *b=alloca(8); o->p=b; }
int main(void){ struct O o={0}; f(&o); free(_Take(o.p)); return 0; }
