#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
__attribute__((noinline)) static int pick(bool *b) { static const int t[2] = {1, 2}; return t[*b]; }
int main(int argc, char **argv) {
  int *_Nullable raw = malloc(sizeof(int)); if (!raw) return 1;
  *raw = 100000 * argc;
  bool *b = (bool *)raw;
  printf("%x\n", pick(b));
  return 0;
}
