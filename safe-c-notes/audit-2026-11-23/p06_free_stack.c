#include <stdlib.h>
#include <stdio.h>
static void drop(int *p) __attribute__((ownership_takes(malloc, 1)));
static void drop(int *p) { free(p); }
int main(void) {
  int x = 3;
  int arr[4] = {0};
  drop(&x);
  free(&arr[2]);
  puts("freed stack");
  return 0;
}
