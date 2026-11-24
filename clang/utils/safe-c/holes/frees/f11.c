#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
struct O{char *_Owned _Nullable p;};
static void f(struct O *o){ char *_Nullable q=o->p; free(q); }
int main(void){ struct O o={malloc(4)}; f(&o); free(_Take(o.p)); return 0; }
