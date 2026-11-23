#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
struct V { int *__counted_by(n) d; size_t n; };
int small[1]; int big[100000];
struct V v = { small, 1 };
_Atomic int stop;
static void *_Nullable flip(void *_Nullable a) {
  while (!stop) { v.n = 1; v.d = small; v.n = 100000; v.d = big; }
  return 0;
}
int main(void) {
  pthread_t t;
  pthread_create(&t, 0, flip, 0);
  for (long i = 0; i < 200000000; i++) v.d[99999] = 0x54545454;
  stop = 1;
  pthread_join(t, 0);
  puts("no trap");
  return 0;
}
