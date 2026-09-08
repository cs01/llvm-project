#include <assert.h>

int abs_val(int x) {
  if (x < 0)
    return -x;
  return x;
}

int main(void) {
  int x;                    // uninitialised == nondeterministic
  assert(abs_val(x) >= 0);  // is this always true?
  return 0;
}
