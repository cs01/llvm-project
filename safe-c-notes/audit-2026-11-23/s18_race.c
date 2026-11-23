#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
struct V { int *__counted_by(n) d; size_t n; };
int small[1]; int big[100000]; int victim[4];
struct V v = { small, 1 };
_Atomic int stop;
static void *_Nullable flip(void *_Nullable a) {
  while (!stop) { v.d = small; v.n = 1; v.d = big; v.n = 100000; }
  return 0;
}
int main(void) {
  pthread_t _Nullable __single t = 0;
  pthread_create(&t, 0, flip, 0);
  for (long i = 0; i < 300000000 && victim[0] == 0 && victim[1] == 0 && victim[2]==0; i++) v.d[1 + (i & 1)] = 0x54545454;
  stop = 1;
  printf("no trap; small=%p big=%p victim=%p: victim %x %x %x\n", (void*)small, (void*)big, (void*)victim, victim[0], victim[1], victim[2]);
  return 0;
}
