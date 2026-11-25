#include <string.h>
int main(void) {
  char b[4] = "abc";
  char *__null_terminated _Nullable s = strchr(b, 'b');
  b[3] = 'x';
  return s ? strlen(s) : 0;
}
