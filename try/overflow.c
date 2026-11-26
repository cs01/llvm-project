#include <stdio.h>

static int sum(const int *__counted_by(n) v, int n) {
  int t = 0;
  for (int i = 0; i <= n; i++)   // one past the end
    t += v[i];
  return t;
}

int main(void) {
  int a[3] = {1, 2, 3};
  printf("%d\n", sum(a, 3));
  return 0;
}
