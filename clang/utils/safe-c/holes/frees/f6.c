#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
struct S{int a; int b;};
int main(void){ struct S *_Nullable s=malloc(sizeof *s); if(!s) return 0; int *p=&s->b; free(p); return 0; }
