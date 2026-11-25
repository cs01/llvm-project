#include <stdio.h>
#include <stdlib.h>
int main(void) {
  static const char fmt[] = "%n";
  int *_Owned _Nullable p = malloc(4);
  if (!p) return 0;
  int *q = p + 1;
  printf(fmt, q);           /* also "%s" with an unterminated buffer, and 0 ? "" : "%s", "x%s" + 1 */
  return 0;
}
