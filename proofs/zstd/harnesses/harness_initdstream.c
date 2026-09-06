// Proof harness: does BIT_initDStream form a pointer outside its input buffer?
//
// bitD->limitPtr = bitD->start + sizeof(bitD->bitContainer) is computed before
// srcSize is compared against that same size, so on the short path -- which the
// function explicitly supports, with a switch over srcSize 2..7 -- the addition
// runs on a buffer smaller than 8 bytes.
//
// C only defines pointer arithmetic up to one past the end of the object, so
// this is the same class of question as ZSTD_overlapCopy8: nothing is
// dereferenced, and nothing misbehaves at runtime, which is why fuzzing does
// not reach it.
//
// The buffer is allocated at exactly srcSize bytes, so an out-of-object pointer
// is visible rather than absorbed by slack in a fixed-size array.
#include "bitstream.h"

size_t nondet_size(void);

void harness(void)
{
    size_t const srcSize = nondet_size();

    // What every caller in the decoder guarantees: a non-empty buffer holding
    // at least srcSize bytes. Bounded above only to keep the state small; the
    // question is entirely about the short path.
    __CPROVER_assume(srcSize >= 1 && srcSize <= 8);

    void *const src = __CPROVER_allocate(srcSize, 0);

    BIT_DStream_t bitD;
    (void)BIT_initDStream(&bitD, src, srcSize);
}
