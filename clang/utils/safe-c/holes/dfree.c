#include <stdlib.h>
int main(void) {
  char *__bidi_indexable _Nullable data = NULL; char *__bidi_indexable _Nullable *p = &data;
  { char *d = (char *)malloc(100); if (d == NULL) { exit(1); } free(d); *p = d; }
  free(data);
}
