#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>
#include <wchar.h>
#include <math.h>
struct c { int n; int *_Static _Nullable __counted_by(n) p; }; int main(void){ struct c x={0}; return (int)frexp(1e30,&x.n); }
