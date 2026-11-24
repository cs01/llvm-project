#include <stdlib.h>
#include <string.h>
#include <safec.h>
#define N _Nullable __single
// //// v16 fread/read into void* alias of a pointer struct
#include <unistd.h>
struct B{int *_Owned _Nullable p;};
int main(void){ struct B s={0}; void *v=&s; (void)read(0,v,sizeof s); return s.p?*s.p:0; }
