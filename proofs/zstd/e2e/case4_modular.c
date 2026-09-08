// Case 4: can a caller be proved from a callee's contract instead of its body?
//
// This is the question that decides whether the approach scales. If every proof
// must inline every callee, the cost is the size of the program. If a call can
// be replaced by its contract, the cost is one function at a time and a codec
// becomes tractable.
//
// 'assigns' is what makes the replacement sound: without a frame, the caller
// cannot assume anything survived the call.
// Declared rather than included: -cc1 has no system include path, and the
// point of the case is the contract plumbing, not the headers.
// __CPROVER_allocate rather than a hand-declared malloc: a malloc the prover
// has not modelled is an uninterpreted function, and the object it returns is
// not known to be n bytes -- which looks exactly like the contract failing.
#include <c_contracts.h>

void *__CPROVER_allocate(unsigned long, int);
void __CPROVER_assert(int, const char *);

// The callee. Verified once, on its own.
void fill_zero(unsigned char *buf, unsigned long n)
  c_writes (buf, n)
  c_post   (c_old(n) == 0 || buf[0] == 0)
{
  // The loop needs its own contract before the function's frame can be
  // checked: goto-instrument refuses assigns-clause instrumentation while a
  // loop remains un-contracted. The two compose, which is the point.
  for (unsigned long i = 0; i < n; i++)
    c_assigns   (c_locations(i, c_range(buf, 0, n)))
    c_invariant (i <= n)
    c_invariant (i == 0 || buf[0] == 0)
    c_decreases (n - i)
  { buf[i] = 0; }
}

// The caller. Must be provable using only fill_zero's contract, with its body
// replaced -- that is what --replace-call-with-contract does.
int caller(unsigned long n)
  c_pre (n >= 4 && n <= 64)
{
  unsigned char *p = __CPROVER_allocate(n, 0);
  int sentinel = 7;
  fill_zero(p, n);
  // If the frame is honoured, the callee cannot have touched this.
  __CPROVER_assert(sentinel == 7, "callee stayed inside its frame");
  // If the postcondition carries, this holds without looking at the body.
  __CPROVER_assert(p[0] == 0, "callee's postcondition is usable by the caller");
  return 0;
}
