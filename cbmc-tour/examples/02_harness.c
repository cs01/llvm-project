#include <assert.h>

// The code under test.
unsigned mid(unsigned lo, unsigned hi) {
  return (lo + hi) / 2;          // classic binary-search overflow bug
}

// The "proof harness": build a symbolic input, constrain it, check a property.
int main(void) {
  unsigned lo = nondet_uint();   // CBMC models any unsigned value
  unsigned hi = nondet_uint();

  __CPROVER_assume(lo <= hi);    // precondition: prune infeasible inputs

  unsigned m = mid(lo, hi);

  __CPROVER_assert(lo <= m && m <= hi, "midpoint lies within the range");
  return 0;
}
