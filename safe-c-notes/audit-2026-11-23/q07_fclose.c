#include <stdio.h>
int main(void) {
  FILE *_Nullable f = fopen("/dev/null", "w");
  if (!f) return 1;
  fclose(f);
  FILE *_Nullable g = fopen("/dev/null", "w");
  fputs("x", f);
  fclose(f);
  puts("used FILE after fclose");
  return 0;
}
