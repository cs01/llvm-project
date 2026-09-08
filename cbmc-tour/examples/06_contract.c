// NOTE: this example is deliberately advanced (is_fresh + loop invariant over
// pointers). Contract instrumentation makes it expensive -- it did not finish
// within several minutes on a laptop-class machine. Start with 07_contract.c.

#include <stdlib.h>

int *find_max(int *arr, size_t len)
  // contract: what the caller must guarantee ...
  __CPROVER_requires(len > 0)
  __CPROVER_requires(__CPROVER_is_fresh(arr, len * sizeof(int)))
  // ... and what the function promises in return
  __CPROVER_ensures(__CPROVER_return_value >= arr &&
                    __CPROVER_return_value < arr + len)
{
  int *best = arr;
  for (size_t i = 1; i < len; i++)
    __CPROVER_loop_invariant(i <= len && best >= arr && best < arr + len)
  {
    if (arr[i] > *best)
      best = &arr[i];
  }
  return best;
}

// DFCC needs a named harness function; --enforce-contract synthesises the
// check (assume requires, run the body, assert ensures) inside find_max itself.
int main(void)
{
  size_t len = nondet_size_t();
  __CPROVER_assume(0 < len && len <= 4);
  int *arr = malloc(len * sizeof(int));
  if (!arr) return 0;
  int *m = find_max(arr, len);
  __CPROVER_assert(m >= arr && m < arr + len, "caller sees the ensures");
  return 0;
}
