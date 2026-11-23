#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
struct V { int *__counted_by(n) d; size_t n; };
int small[1]; int victim[8]; int big[100000];
struct V v = { small, 1 };
_Atomic int stop;
int dummy;
__attribute__((noinline)) static void set_small(void) { v.d = small; v.n = 1; }
__attribute__((noinline)) static void set_big(void) { v.d = big; v.n = 100000; }
static void *_Null_unspecified flip(void *_Null_unspecified a) {
  while (!stop) { set_small(); set_big(); }
  return &dummy;
}
int main(void) {
  pthread_t _Nullable __single t = 0;
  __unsafe { pthread_create(&t, 0, flip, &dummy); } /* only the spawn is unsafe */
  long i;
  for (i = 0; i < 2000000000 && victim[0] == 0 && victim[1] == 0 && victim[2] == 0 && victim[3] == 0; i++)
    if (v.n > 4) v.d[1 + (i & 3)] = 0x54545454;
  stop = 1;
  printf("no trap after %ld iterations; small=%p victim=%p: victim = %x %x %x %x\n", i, (void*)small, (void*)victim, victim[0], victim[1], victim[2], victim[3]);
  return 0;
}
