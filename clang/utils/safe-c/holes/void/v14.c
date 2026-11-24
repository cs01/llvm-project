#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v14 char* launder to struct
struct B{int *_Owned _Nullable p;};
int main(void){ char buf[8]={1,2,3,4,5,6,7,8}; struct B *b=(struct B*)buf; return b->p?*b->p:0; }
