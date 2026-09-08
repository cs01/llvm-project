#include <assert.h>

int sum_to(unsigned n) {
  int s = 0;
  for (unsigned i = 0; i <= n; i++)   // note: <= , so it runs n+1 times
    s += i;
  return s;
}

int main(void) {
  unsigned n = nondet_uint();
  __CPROVER_assume(n <= 4);
  assert(sum_to(n) == (int)(n * (n + 1) / 2));
  return 0;
}
