#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v01 cast back to the wrong type
struct A{int x;}; struct B{int *_Owned _Nullable p;};
int main(void){ struct A a={1}; void *v=&a; struct B *b=(struct B*)v; return b->p?*b->p:0; }
