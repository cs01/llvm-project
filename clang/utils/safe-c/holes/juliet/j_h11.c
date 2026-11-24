#include <stdlib.h>
typedef struct { char *_Owned _Nullable a; } st;
static void use(st *_Exclusive s) { if (s->a != NULL) { s->a[0] = 1; } }
int main(void) {
  st u;
  char *d = (char *)malloc(5);
  if (d == NULL) { exit(1); }
  free(d);
  u.a = d;
  use(&u);
}
