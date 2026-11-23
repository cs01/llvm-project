#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
int target = 5;
int main(int argc, char **argv) {
  long addr = (long)&target;
  void *v = (void *)addr;
  void *w = 0;
  sscanf("0x41414141", "%p", &w);
  free(w);
  puts("free of forged pointer returned");
  return 0;
}
