#include <stdlib.h>
int main(void) {
  char *__null_terminated _Nullable e = 0;
  strtol("12x", &e, 10);
  if (e) *e = 'y';
  return 0;
}
