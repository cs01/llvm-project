#include <stdlib.h>
#include <string.h>
#define VEC_NAME names
#define VEC_TYPE char *_Owned _Nullable __null_terminated
#define VEC_BORROW char *_Nullable __null_terminated
#define VEC_OWNED 1
#include "vec.h"
int main(void) { struct names n = {0}; (void)names_add(&n, strdup("a")); char *_Nullable __null_terminated s = names_at(&n, 0); for (int k = 0; k < 50; k++) (void)names_add(&n, strdup("b")); int r = s ? s[0] : 0; names_free(&n); return r; }
