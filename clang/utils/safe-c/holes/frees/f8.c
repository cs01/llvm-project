#include <stdlib.h>
#include <string.h>
#include <alloca.h>
#include <safec.h>
#define N _Nullable __single
int main(void){ const char *p="x"; free((void*)p); return 0; }
