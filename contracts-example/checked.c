#include "contracts.h"

// The contracts in contracts.h are checked at every call site, at compile time.
// Nothing here traps at runtime and nothing runs an analyzer: this is an
// ordinary warning from an ordinary build.

void bugs(void) {
  int *b = allocate(0);        // 'requires (n > 0)' is violated
  put(b, 8, 8, 1);             // 'requires (i < len)' is violated: 8 < 8 is false
  buf_free_if_needed();
}

void correct(void) {
  int *b = allocate(8);        // 'ensures' says the result is non-null ...
  put(b, 8, 0, 1);             // ... so this call is discharged. Silent.
}

void unknown_stays_quiet(int c, int *maybe) {
  int *b = maybe;
  if (c) b = 0;
  put(b, 8, 0, 1);             // the two edges disagree: the pass says nothing
}

void buf_free_if_needed(void);
