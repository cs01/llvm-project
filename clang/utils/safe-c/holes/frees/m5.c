#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
struct _View V{char *N p;};
static void f(struct V *out){ char *b=alloca(8); b[0]=1; struct V v={b}; *out=v; }
int main(void){ struct V v={0}; f(&v); return v.p?*v.p:0; }
