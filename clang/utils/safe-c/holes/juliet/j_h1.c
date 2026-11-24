#include <stdlib.h>
typedef union { char *_Owned __bidi_indexable _Nullable a; char *_Owned __bidi_indexable _Nullable b; } un;
int main(void) {
  un u;
  char *d = (char *)malloc(5);
  if (d == NULL) { exit(1); }
  free(d);
  u.a = d;
  { char *e = _Take(u.b); free(e); }
}
