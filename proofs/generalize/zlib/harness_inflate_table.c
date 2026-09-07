// Does inflate_table need more than its doc-comment promises?
//
// inftrees.c documents:
//
//   The code lengths are lens[0..codes-1].  The result starts at *table,
//   whose indices are 0..2^bits-1.  work is a writable array of at least
//   lens shorts [...]
//
// "indices are 0..2^bits-1" is the interesting clause. zlib's own callers pass
// state->codes, an array of ENOUGH entries (852 for LENS, 592 for DISTS), which
// is far more than 2^bits for the bits values actually used. If the code can
// write past 2^bits then the sentence above is not the contract the body needs,
// and a reader who implemented against it would be wrong.
//
// So: give table exactly the 2^bits entries the comment promises, and nothing
// more. lens is fully symbolic -- it comes from the compressed stream.
#include "inftrees.c"

unsigned nondet_uint(void);
unsigned short nondet_ushort(void);

#define CODES_MAX 12u
#define ROOT_BITS  4u

void harness(void)
{
    unsigned codes = nondet_uint();
    __CPROVER_assume(codes >= 1 && codes <= CODES_MAX);

    unsigned short lens[CODES_MAX];
    unsigned short work[CODES_MAX];
    for (unsigned i = 0; i < CODES_MAX; i++) {
        lens[i] = nondet_ushort();
        // MAXBITS is 15; a length above it is a caller error, not this
        // function's business, and zlib's callers cannot produce one.
        __CPROVER_assume(lens[i] <= MAXBITS);
    }

    // Exactly what the doc-comment entitles a caller to provide.
    static code table[1u << ROOT_BITS];
    code *next = table;
    unsigned bits = ROOT_BITS;

    (void)inflate_table(LENS, lens, codes, &next, &bits, work);
}
