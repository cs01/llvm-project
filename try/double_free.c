#include <stdlib.h>

int main(void) {
  char *_Owned _Nullable s = malloc(16);
  free(s);
  free(s);                // freed twice
  return 0;
}
