#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// w04 own memcpy dup owner through void params (double free)
struct B{int *_Owned _Nullable p;};
static void cp(void *__sized_by(n) d,const void *__sized_by(n) s,size_t n){ memcpy(d,s,n);}
int main(void){ struct B a={malloc(4)}, b={0}; cp(&b,&a,sizeof a); free(_Take(a.p)); free(_Take(b.p)); return 0; }
