#include <stdlib.h>
#include <stdio.h>
int main(void) {
  int *_Nullable arr[2] = {0, 0};
  arr[0] = malloc(sizeof(int));
  arr[1] = arr[0];
  free(arr[0]);
  int *_Nullable s = malloc(sizeof(int)); if (!s) return 1; *s = 0x50505050;
  if (arr[1]) printf("%x\n", *arr[1]);
  return 0;
}
