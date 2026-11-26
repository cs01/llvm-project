#include <stdlib.h>
#include <string.h>
#define VEC_NAME names
#define VEC_TYPE char *_Owned _Nullable __null_terminated
#define VEC_BORROW char *_Nullable __null_terminated
#define VEC_OWNED 1
#include "vec.h"
int main(void) { struct names n = {0}; (void)names_add(&n, strdup("a")); struct names m = n; names_free(&n); names_free(&m); return 0; }
