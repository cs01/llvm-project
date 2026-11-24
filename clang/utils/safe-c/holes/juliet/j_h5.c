#include <stdlib.h>
static void sink(char *_Owned __bidi_indexable _Nullable *_Exclusive __counted_by(5) a) { char *data = _Take(a[2]); free(data); }
int main(void) {
  char *_Owned __bidi_indexable _Nullable a[5];
  char *data = (char *)__builtin_alloca(100);
  a[2] = data;
  sink(a);
}
