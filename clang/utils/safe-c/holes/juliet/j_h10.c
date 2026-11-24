#include <stdlib.h>
typedef struct { char *_Owned _Nullable a; } st;
static void sink(st *_Exclusive s) { free(_Take(s->a)); }
int main(void) {
  st u;
  char *d = (char *)malloc(5);
  if (d == NULL) { exit(1); }
  free(d);
  u.a = d;
  sink(&u);
}
