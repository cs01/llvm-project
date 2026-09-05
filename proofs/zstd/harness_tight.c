// Proof harness: does BIT_lookBits stay in bounds?
//
// BIT_getMiddleBits indexes BIT_mask[nbBits], and BIT_MASK_SIZE is 32, while
// BIT_lookBits documents maxNbBits == 56 on a 64-bit target. On any target
// that is not x86_64 the masking path is the array one, so the question is
// whether nbBits can reach 32.
#include "bitstream.h"

unsigned int nondet_uint(void);
unsigned long long nondet_u64(void);

void harness(void)
{
    BIT_DStream_t bitD;
    U32 const nbBits = nondet_uint();
    bitD.bitContainer = nondet_u64();
    bitD.bitsConsumed = nondet_uint();

    // The contract as the header documents it, and nothing more.
    __CPROVER_assume(nbBits <= 31);
    __CPROVER_assume(bitD.bitsConsumed <= 64);
    __CPROVER_assume(bitD.bitsConsumed + nbBits <= 64);

    (void)BIT_lookBits(&bitD, nbBits);
}
