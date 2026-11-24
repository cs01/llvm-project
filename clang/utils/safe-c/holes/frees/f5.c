#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
static void f(int *p){ int *q=p; free(q); }
int main(void){ int x=0; f(&x); return 0; }
