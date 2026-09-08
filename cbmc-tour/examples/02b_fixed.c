#include <assert.h>

// The fix: compute the offset, then add. Never leaves the [lo,hi] range.
unsigned mid(unsigned lo, unsigned hi) {
  return lo + (hi - lo) / 2;
}

int main(void) {
  unsigned lo = nondet_uint();
  unsigned hi = nondet_uint();

  __CPROVER_assume(lo <= hi);

  unsigned m = mid(lo, hi);

  __CPROVER_assert(lo <= m && m <= hi, "midpoint lies within the range");
  return 0;
}
