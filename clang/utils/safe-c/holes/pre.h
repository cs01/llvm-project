#include <stdlib.h>
#include <string.h>
#define N _Nullable __single
struct own { int *_Owned _Nullable p; };
struct _View box { int *_Nullable p; };
static void set(int *N *out, int *in) { *out = in; }
static void setbox(struct box *_Exclusive out, struct box in) { *out = in; }
static int gi;
#define B int *_Nullable b = malloc(4); if (!b) return 0; *b = 1;
#define T struct box *_Owned _Nullable t = calloc(1, sizeof *t); if (!t) { free(b); return 0; }
#define RET free(b); return o ? *o : 0;
