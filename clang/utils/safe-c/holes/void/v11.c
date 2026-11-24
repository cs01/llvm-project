#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v11 va_arg struct with pointers (hole 14)
#include <stdarg.h>
struct B{int *_Owned _Nullable p;};
static int f(int n,...){ va_list ap; va_start(ap,n); struct B b=va_arg(ap,struct B); va_end(ap); return b.p?*b.p:0; }
int main(void){ return f(1,0x4141414141L); }
