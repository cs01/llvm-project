/*
   LZ4 - Fast LZ compression algorithm
   Copyright (c) Yann Collet. All rights reserved.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   You can contact the author at :
    - LZ4 homepage : http://www.lz4.org
    - LZ4 source repository : https://github.com/lz4/lz4
*/

/*-************************************
*  Tuning parameters
**************************************/
/*
 * LZ4_HEAPMODE :
 * Select how stateless compression functions like `LZ4_compress_default()`
 * allocate memory for their hash table,
 * in memory stack (0:default, fastest), or in memory heap (1:requires malloc()).
 */
#ifndef LZ4_HEAPMODE
#  define LZ4_HEAPMODE 0
#endif


/*-************************************
*  CPU Feature Detection
**************************************/
/* LZ4_FORCE_MEMORY_ACCESS
 * By default, access to unaligned memory is controlled by `memcpy()`, which is safe and portable.
 * Unfortunately, on some target/compiler combinations, the generated assembly is sub-optimal.
 * The below switch allow to select different access method for improved performance.
 * Method 0 (default) : use `memcpy()`. Safe and portable.
 * Method 1 : `__packed` statement. It depends on compiler extension (ie, not portable).
 *            This method is safe if your compiler supports it, and *generally* as fast or faster than `memcpy`.
 * Method 2 : direct access. This method is portable but violate C standard.
 *            It can generate buggy code on targets which assembly generation depends on alignment.
 *            But in some circumstances, it's the only known way to get the most performance (ie GCC + ARMv6)
 * See https://fastcompression.blogspot.fr/2015/08/accessing-unaligned-memory.html for details.
 * Prefer these methods in priority order (0 > 1 > 2)
 */
#ifndef LZ4_FORCE_MEMORY_ACCESS   /* can be defined externally */
#  if defined(__GNUC__) && \
  ( defined(__ARM_ARCH_6__) || defined(__ARM_ARCH_6J__) || defined(__ARM_ARCH_6K__) \
  || defined(__ARM_ARCH_6Z__) || defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) \
  || (defined(__riscv) && defined(__riscv_zicclsm)) )
#    define LZ4_FORCE_MEMORY_ACCESS 2
#  elif (defined(__INTEL_COMPILER) && !defined(_WIN32)) || defined(__GNUC__) || defined(_MSC_VER)
#    define LZ4_FORCE_MEMORY_ACCESS 1
#  endif
#endif

/*
 * LZ4_FORCE_SW_BITCOUNT
 * Define this parameter if your target system or compiler does not support hardware bit count
 */
#if defined(_MSC_VER) && defined(_WIN32_WCE)   /* Visual Studio for WinCE doesn't support Hardware bit count */
#  undef  LZ4_FORCE_SW_BITCOUNT  /* avoid double def */
#  define LZ4_FORCE_SW_BITCOUNT
#endif



/*-************************************
*  Dependency
**************************************/
/*
 * LZ4_SRC_INCLUDED:
 * Amalgamation flag, whether lz4.c is included
 */
#ifndef LZ4_SRC_INCLUDED
#  define LZ4_SRC_INCLUDED 1
#endif

#ifndef LZ4_DISABLE_DEPRECATE_WARNINGS
#  define LZ4_DISABLE_DEPRECATE_WARNINGS /* due to LZ4_decompress_safe_withPrefix64k */
#endif

#ifndef LZ4_STATIC_LINKING_ONLY
#  define LZ4_STATIC_LINKING_ONLY
#endif
#include "lz4.h"
/* see also "memory routines" below */


/*-************************************
*  Compiler Options
**************************************/
#if defined(_MSC_VER) && (_MSC_VER >= 1400)  /* Visual Studio 2005+ */
#  include <intrin.h>               /* only present in VS2005+ */
#  pragma warning(disable : 4127)   /* disable: C4127: conditional expression is constant */
#  pragma warning(disable : 6237)   /* disable: C6237: conditional expression is always 0 */
#  pragma warning(disable : 6239)   /* disable: C6239: (<non-zero constant> && <expression>) always evaluates to the result of <expression> */
#  pragma warning(disable : 6240)   /* disable: C6240: (<expression> && <non-zero constant>) always evaluates to the result of <expression> */
#  pragma warning(disable : 6326)   /* disable: C6326: Potential comparison of a constant with another constant */
#endif  /* _MSC_VER */

#ifndef LZ4_FORCE_INLINE
#  if defined (_MSC_VER) && !defined (__clang__)    /* MSVC */
#    define LZ4_FORCE_INLINE static __forceinline
#  else
#    if defined (__cplusplus) || defined (__STDC_VERSION__) && __STDC_VERSION__ >= 199901L   /* C99 */
#      if defined (__GNUC__) || defined (__clang__)
#        define LZ4_FORCE_INLINE static inline __attribute__((always_inline))
#      else
#        define LZ4_FORCE_INLINE static inline
#      endif
#    else
#      define LZ4_FORCE_INLINE static
#    endif /* __STDC_VERSION__ */
#  endif  /* _MSC_VER */
#endif /* LZ4_FORCE_INLINE */

/* LZ4_FORCE_NOINLINE : keeps a rarely used function out of its callers' hot loops */
#ifndef LZ4_FORCE_NOINLINE
#  if defined(_MSC_VER)
#    define LZ4_FORCE_NOINLINE static __declspec(noinline)
#  elif defined(__GNUC__) || defined(__clang__)
#    define LZ4_FORCE_NOINLINE static __attribute__((noinline))
#  else
#    define LZ4_FORCE_NOINLINE static
#  endif
#endif

/* LZ4_FORCE_O2 and LZ4_FORCE_INLINE
 * gcc on ppc64le generates an unrolled SIMDized loop for LZ4_wildCopy8,
 * together with a simple 8-byte copy loop as a fall-back path.
 * However, this optimization hurts the decompression speed by >30%,
 * because the execution does not go to the optimized loop
 * for typical compressible data, and all of the preamble checks
 * before going to the fall-back path become useless overhead.
 * This optimization happens only with the -O3 flag, and -O2 generates
 * a simple 8-byte copy loop.
 * With gcc on ppc64le, all of the LZ4_decompress_* and LZ4_wildCopy8
 * functions are annotated with __attribute__((optimize("O2"))),
 * and also LZ4_wildCopy8 is forcibly inlined, so that the O2 attribute
 * of LZ4_wildCopy8 does not affect the compression speed.
 */
#if defined(__PPC64__) && defined(__LITTLE_ENDIAN__) && defined(__GNUC__) && !defined(__clang__)
#  define LZ4_FORCE_O2  __attribute__((optimize("O2")))
#  undef LZ4_FORCE_INLINE
#  define LZ4_FORCE_INLINE  static __inline __attribute__((optimize("O2"),always_inline))
#else
#  define LZ4_FORCE_O2
#endif

#if (defined(__GNUC__) && (__GNUC__ >= 3)) || (defined(__INTEL_COMPILER) && (__INTEL_COMPILER >= 800)) || defined(__clang__)
#  define expect(expr,value)    (__builtin_expect ((expr),(value)) )
#else
#  define expect(expr,value)    (expr)
#endif

#ifndef likely
#define likely(expr)     expect((expr) != 0, 1)
#endif
#ifndef unlikely
#define unlikely(expr)   expect((expr) != 0, 0)
#endif

/* Should the alignment test prove unreliable, for some reason,
 * it can be disabled by setting LZ4_ALIGN_TEST to 0 */
#ifndef LZ4_ALIGN_TEST  /* can be externally provided */
# define LZ4_ALIGN_TEST 1
#endif


/*-************************************
*  Memory routines
**************************************/

/*! LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION :
 *  lz4 never allocates memory by itself : there is no default allocator (malloc(), calloc(), free()).
 *  Memory only comes from allocators given by the user :
 *  LZ4_malloc(), LZ4_calloc() and LZ4_free() with LZ4_USER_MEMORY_FUNCTIONS (below),
 *  or LZ4F_CustomMem for LZ4F_* functions.
 *  Without them, functions which would allocate fail, as on allocation failure :
 *  LZ4_createStream(), LZ4_createStreamDecode() and LZ4_createStreamHC() return NULL,
 *  and LZ4F_* functions creating a context without LZ4F_CustomMem return an error.
 *  Invoking them generates a deprecation warning (LZ4_NOALLOC_WARNING, LZ4F_NOALLOC_WARNING).
 *  Stateless compression functions keep their state on stack by default (LZ4_HEAPMODE, LZ4HC_HEAPMODE).
 *
 *  Note that this is a compile-time switch, which changes the behavior of public/stable LZ4 v1 API functions :
 *  we don't recommend using it to generate a library for distribution.
 */
#if defined(LZ4_USER_MEMORY_FUNCTIONS)
/* memory management functions can be customized by user project.
 * Below functions must exist somewhere in the Project
 * and be available at link time. */
void* LZ4_malloc(size_t s);
void* LZ4_calloc(size_t n, size_t s);
void  LZ4_free(void* p);
# define ALLOC(s)          LZ4_malloc(s)
# define ALLOC_AND_ZERO(s) LZ4_calloc(1,s)
# define FREEMEM(p)        LZ4_free(p)
#elif defined(LZ4_STATIC_LINKING_ONLY_DISABLE_MEMORY_ALLOCATION)
# define ALLOC(s)          NULL   /* no default allocator */
# define ALLOC_AND_ZERO(s) NULL
# define FREEMEM(p)        ((void)(p))
#else
# include <stdlib.h>   /* malloc, calloc, free */
# define ALLOC(s)          malloc(s)
# define ALLOC_AND_ZERO(s) calloc(1,s)
# define FREEMEM(p)        free(p)
#endif

#if ! LZ4_FREESTANDING
#  include <string.h>   /* memset, memcpy */
#endif
#if !defined(LZ4_memset)
#  define LZ4_memset(p,v,s) memset((p),(v),(s))
#endif
#define MEM_INIT(p,v,s)   LZ4_memset((p),(v),(s))


/*-************************************
*  Common Constants
**************************************/
#define MINMATCH 4

#define WILDCOPYLENGTH 8
#define LASTLITERALS   5   /* see ../doc/lz4_Block_format.md#parsing-restrictions */
#define MFLIMIT       12   /* see ../doc/lz4_Block_format.md#parsing-restrictions */
#define MATCH_SAFEGUARD_DISTANCE  ((2*WILDCOPYLENGTH) - MINMATCH)   /* ensure it's possible to write 2 x wildcopyLength without overflowing output buffer */
#define FASTLOOP_SAFE_DISTANCE 64
static const int LZ4_minLength = (MFLIMIT+1);

#define KB *(1 <<10)
#define MB *(1 <<20)
#define GB *(1U<<30)

#define LZ4_DISTANCE_ABSOLUTE_MAX 65535
#if (LZ4_DISTANCE_MAX > LZ4_DISTANCE_ABSOLUTE_MAX)   /* max supported by LZ4 format */
#  error "LZ4_DISTANCE_MAX is too big : must be <= 65535"
#endif

#define ML_BITS  4
#define ML_MASK  ((1U<<ML_BITS)-1)
#define RUN_BITS (8-ML_BITS)
#define RUN_MASK ((1U<<RUN_BITS)-1)


/*-************************************
*  Error detection
**************************************/
#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=1)
#  include <assert.h>
#else
#  ifndef assert
#    define assert(condition) ((void)0)
#  endif
#endif

#define LZ4_STATIC_ASSERT(c)   { enum { LZ4_static_assert = 1/(int)(!!(c)) }; }   /* use after variable declarations */

#if defined(LZ4_DEBUG) && (LZ4_DEBUG>=2)
#  include <stdio.h>
   static int g_debuglog_enable = 1;
#  define DEBUGLOG(l, ...) {                          \
        if ((g_debuglog_enable) && (l<=LZ4_DEBUG)) {  \
            fprintf(stderr, __FILE__  " %i: ", __LINE__); \
            fprintf(stderr, __VA_ARGS__);             \
            fprintf(stderr, " \n");                   \
    }   }
#else
#  define DEBUGLOG(l, ...) {}    /* disabled */
#endif

static int LZ4_isAligned(const void* ptr, size_t alignment)
{
    return ((size_t)ptr & (alignment -1)) == 0;
}


/*-************************************
*  Types
**************************************/
#include <limits.h>
#if defined(__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
# include <stdint.h>
  typedef unsigned char BYTE; /*uint8_t not necessarily blessed to alias arbitrary type*/
  typedef uint16_t      U16;
  typedef uint32_t      U32;
  typedef  int32_t      S32;
  typedef uint64_t      U64;
  typedef uintptr_t     uptrval;
#else
# if UINT_MAX != 4294967295UL
#   error "LZ4 code (when not C++ or C99) assumes that sizeof(int) == 4"
# endif
  typedef unsigned char       BYTE;
  typedef unsigned short      U16;
  typedef unsigned int        U32;
  typedef   signed int        S32;
  typedef unsigned long long  U64;
  typedef size_t              uptrval;   /* generally true, except OpenVMS-64 */
#endif

#if defined(__x86_64__)
  typedef U64    reg_t;   /* 64-bits in x32 mode */
#else
  typedef size_t reg_t;   /* 32-bits in x32 mode */
#endif

typedef enum {
    notLimited = 0,
    limitedOutput = 1,
    fillOutput = 2
} limitedOutput_directive;


/*-************************************
*  Reading and writing into memory
**************************************/

/**
 * LZ4 relies on memcpy with a constant size being inlined. In freestanding
 * environments, the compiler can't assume the implementation of memcpy() is
 * standard compliant, so it can't apply its specialized memcpy() inlining
 * logic. When possible, use __builtin_memcpy() to tell the compiler to analyze
 * memcpy() as if it were standard compliant, so it can inline it in freestanding
 * environments. This is needed when decompressing the Linux Kernel, for example.
 */
#if !defined(LZ4_memcpy)
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LZ4_memcpy(dst, src, size) __builtin_memcpy(dst, src, size)
#  else
#    define LZ4_memcpy(dst, src, size) memcpy(dst, src, size)
#  endif
#endif

#if !defined(LZ4_memmove)
#  if defined(__GNUC__) && (__GNUC__ >= 4)
#    define LZ4_memmove __builtin_memmove
#  else
#    define LZ4_memmove memmove
#  endif
#endif

static unsigned LZ4_isLittleEndian(void)
{
    const union { U32 u; BYTE c[4]; } one = { 1 };   /* don't use static : performance detrimental */
    return one.c[0];
}

#if defined(__GNUC__) || defined(__INTEL_COMPILER)
#define LZ4_PACK( __Declaration__ ) __Declaration__ __attribute__((__packed__))
#elif defined(_MSC_VER)
#define LZ4_PACK( __Declaration__ ) __pragma( pack(push, 1) ) __Declaration__ __pragma( pack(pop))
#endif


/* Safe C port: the LZ4_FORCE_MEMORY_ACCESS 1 and 2 variants (packed structs, direct
 * dereference of unaligned words) are removed; memcpy into an integer is the only one.
 * Every helper takes byte pointers that carry their bounds (__bidi_indexable) instead
 * of `void*`: a byte view of `void*` is rejected (bytes written through it could forge
 * a pointer), and a plain (single) pointer allows no arithmetic. The helpers are
 * always inlined, so the wide pointers cost no calling convention. */
/* LZ4_AUDITED (Safe C): the compressor and decompressor cores and the memory
 * primitives they inline are compiled without run-time bounds checks, to run
 * within 1% of plain C. This is an audited promise, not a proof:
 *  - lz4 checks every access itself, against ilimit/iend/matchlimit for input,
 *    oend/olimit for output, and hash-table indices by construction (a hash is
 *    shifted down to the table's bit width). LZ4_decompress_safe is meant to be
 *    safe for any input and is fuzzed continuously upstream (OSS-Fuzz).
 *  - Safe C still checks everything at the API boundary: the buffers passed in
 *    carry their sizes (__counted_by), and every call into these functions is
 *    checked against them.
 *  - The compressor's unlimited-output mode trusts that dst holds
 *    LZ4_compressBound(srcSize); LZ4_compress_generic_validated checks that
 *    on entry (see below) and otherwise takes the limited-output path.
 * TODO(safe-c): remove once the compiler eliminates checks a loop guard
 * implies (ConstraintElimination: pointer differences vs pointer compares,
 * forward-only pointers); see PORT.md, "Speed". */
#if defined(__SAFE_C__)
#define LZ4_AUDITED __unsafe _No_bounds_checks
#else
#define LZ4_AUDITED
#endif

LZ4_AUDITED static U16 LZ4_read16(const BYTE* __bidi_indexable memPtr)
{
    U16 val; LZ4_memcpy(&val, memPtr, sizeof(val)); return val;
}

LZ4_AUDITED static U32 LZ4_read32(const BYTE* __bidi_indexable memPtr)
{
    U32 val; LZ4_memcpy(&val, memPtr, sizeof(val)); return val;
}

LZ4_AUDITED static reg_t LZ4_read_ARCH(const BYTE* __bidi_indexable memPtr)
{
    reg_t val; LZ4_memcpy(&val, memPtr, sizeof(val)); return val;
}

LZ4_AUDITED static void LZ4_write16(BYTE* __bidi_indexable memPtr, U16 value)
{
    LZ4_memcpy(memPtr, &value, sizeof(value));
}

LZ4_AUDITED static void LZ4_write32(BYTE* __bidi_indexable memPtr, U32 value)
{
    LZ4_memcpy(memPtr, &value, sizeof(value));
}


static U16 LZ4_readLE16(const BYTE* __bidi_indexable memPtr)
{
    if (LZ4_isLittleEndian()) {
        return LZ4_read16(memPtr);
    } else {
        const BYTE* p = memPtr;
        return (U16)((U16)p[0] | (p[1]<<8));
    }
}

#ifdef LZ4_STATIC_LINKING_ONLY_ENDIANNESS_INDEPENDENT_OUTPUT
static U32 LZ4_readLE32(const BYTE* __bidi_indexable memPtr)
{
    if (LZ4_isLittleEndian()) {
        return LZ4_read32(memPtr);
    } else {
        const BYTE* p = memPtr;
        return (U32)p[0] | (p[1]<<8) | (p[2]<<16) | (p[3]<<24);
    }
}
#endif

static void LZ4_writeLE16(BYTE* __bidi_indexable memPtr, U16 value)
{
    if (LZ4_isLittleEndian()) {
        LZ4_write16(memPtr, value);
    } else {
        BYTE* p = memPtr;
        p[0] = (BYTE) value;
        p[1] = (BYTE)(value>>8);
    }
}

/* Safe C: a wild copy writes whole stripes of `piece` bytes up to and past
 * dstEnd, so it may touch up to the run rounded up to `stripe`. That run is
 * checked against both buffers once, here (a trap if it does not fit), and
 * the stripes are then copied without per-stripe checks: the same safety as
 * checking every copy, at one check per call. Without Safe C it is the plain
 * loop. TODO(safe-c): the compiler should hoist this check by itself. */
LZ4_FORCE_INLINE void LZ4_wildCopyRun(BYTE* __bidi_indexable d, const BYTE* __bidi_indexable s,
                                      const BYTE* __bidi_indexable e, size_t piece, size_t stripe)
{
    size_t const len = e > d ? (size_t)(e - d) : 1;
    size_t const n = (len + stripe - 1) / stripe * stripe;
#if defined(__SAFE_C__)
    if (d < (BYTE*)__ptr_lower_bound(d) || n > (size_t)((BYTE*)__ptr_upper_bound(d) - d) ||
        s < (const BYTE*)__ptr_lower_bound(s) || n > (size_t)((const BYTE*)__ptr_upper_bound(s) - s))
        __builtin_trap();
    __unsafe {
        BYTE* __unsafe_indexable du = d;
        const BYTE* __unsafe_indexable su = s;
        for (size_t i = 0; i < n; i += piece)
            LZ4_memcpy(__unsafe_forge_bidi_indexable(BYTE*, du + i, piece),
                       __unsafe_forge_bidi_indexable(const BYTE*, su + i, piece), piece);
    }
#else
    for (size_t i = 0; i < n; i += piece) LZ4_memcpy(d + i, s + i, piece);
#endif
}

/* customized variant of memcpy, which can overwrite up to 8 bytes beyond dstEnd */
LZ4_AUDITED LZ4_FORCE_INLINE
void LZ4_wildCopy8(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd)
{
    BYTE* d = dstPtr;
    const BYTE* s = srcPtr;
    BYTE* const e = dstEnd;

    do { LZ4_memcpy(d,s,8); d+=8; s+=8; } while (d<e);
}

static const unsigned inc32table[8] = {0, 1, 2,  1,  0,  4, 4, 4};
static const int      dec64table[8] = {0, 0, 0, -1, -4,  1, 2, 3};


#ifndef LZ4_FAST_DEC_LOOP
#  if defined __i386__ || defined _M_IX86 || defined __x86_64__ || defined _M_X64
#    define LZ4_FAST_DEC_LOOP 1
#  elif defined(__aarch64__)
#    define LZ4_FAST_DEC_LOOP 1
#  elif defined(__PPC64__)
#    define LZ4_FAST_DEC_LOOP 1
#  elif defined(__riscv) && (__riscv_xlen == 64) && defined(__riscv_misaligned_fast)
#    define LZ4_FAST_DEC_LOOP 1   /* otherwise, unaligned memcpy() become byte copies, defeating the fast loop */
#  else
#    define LZ4_FAST_DEC_LOOP 0
#  endif
#endif

/* LZ4_memcpy_using_offset_base() :
 * copy a match of length >= MINMATCH (srcPtr + offset == dstPtr, overlap allowed),
 * can overwrite up to 12 bytes beyond dstEnd */
LZ4_FORCE_INLINE void
LZ4_memcpy_using_offset_base(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd, const size_t offset)
{
    assert(srcPtr + offset == dstPtr);
    if (unlikely(offset < 8)) {
        LZ4_write32(dstPtr, 0);   /* silence an msan warning when offset==0 */
        dstPtr[0] = srcPtr[0];
        dstPtr[1] = srcPtr[1];
        dstPtr[2] = srcPtr[2];
        dstPtr[3] = srcPtr[3];
        srcPtr += inc32table[offset];
        LZ4_memcpy(dstPtr+4, srcPtr, 4);
        srcPtr -= dec64table[offset];
    } else {
        LZ4_memcpy(dstPtr, srcPtr, 8);
        srcPtr += 8;
    }
    dstPtr += 8;

    /* srcPtr is now at least 8 bytes behind dstPtr */
    LZ4_memcpy(dstPtr, srcPtr, 8);
    if (dstEnd > dstPtr + 8) { LZ4_wildCopy8(dstPtr + 8, srcPtr + 8, dstEnd); }
}


#if LZ4_FAST_DEC_LOOP

/* LZ4_wildCopy64 is faster on aarch64, x86-64 and ppc64, but not on 32-bit x86.
 * On wasm, it depends on the host cpu (faster on Apple silicon, slower on x86-64) :
 * it's not enabled by default, but can be, with LZ4_FAST_DEC_LOOP, from the build command */
#ifndef LZ4_WILDCOPY64
#  if defined(__aarch64__) || defined(__x86_64__) || defined(_M_X64) || defined(__PPC64__)
#    define LZ4_WILDCOPY64 1
#  else
#    define LZ4_WILDCOPY64 0
#  endif
#endif

#if LZ4_WILDCOPY64
/* customized variant of memcpy, which can overwrite up to 64 bytes beyond dstEnd */
LZ4_AUDITED LZ4_FORCE_INLINE void
LZ4_wildCopy64(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd)
{
    BYTE* d = dstPtr;
    const BYTE* s = srcPtr;
    BYTE* const e = dstEnd;

    do { LZ4_memcpy(d,s,64); d+=64; s+=64; } while (d<e);
}
#endif

/* customized variant of memcpy, which can overwrite up to 32 bytes beyond dstEnd
 * this version copies two times 16 bytes (instead of one time 32 bytes)
 * because it must be compatible with offsets >= 16. */
LZ4_AUDITED LZ4_FORCE_INLINE void
LZ4_wildCopy32(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd)
{
    BYTE* d = dstPtr;
    const BYTE* s = srcPtr;
    BYTE* const e = dstEnd;

    do { LZ4_memcpy(d,s,16); LZ4_memcpy(d+16,s+16,16); d+=32; s+=32; } while (d<e);
}

/* customized variant of memcpy, which can overwrite up to 16 bytes beyond dstEnd,
 * for non-overlapping buffers */
LZ4_AUDITED LZ4_FORCE_INLINE void
LZ4_wildCopy16(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd)
{
    BYTE* d = dstPtr;
    const BYTE* s = srcPtr;
    BYTE* const e = dstEnd;

    do { LZ4_memcpy(d,s,16); d+=16; s+=16; } while (d<e);
}

/* Fast-loop copies without per-copy bounds checks (Safe C).
 * The fast decode loop of LZ4_decompress_generic runs only while
 * `oend - op >= FASTLOOP_SAFE_DISTANCE` (64), checked on entry and before every
 * match that could cross it, and copies literals only when
 * `ip <= iend - 17`, checked before the copy. Under those checks the
 * fixed-size copies below stay inside the buffers, so each is done with one
 * audited unchecked access instead of nine compare-and-branch checks (the
 * profile showed them as most of decompression time). Use them nowhere else.
 * TODO(safe-c): drop these once the compiler hoists and removes bounds checks
 * implied by a loop guard (see PORT.md, "Speed"). */
LZ4_FORCE_INLINE void LZ4_fastCopy16(BYTE* __bidi_indexable op, const BYTE* __bidi_indexable ip)
{
    /* op: 64 bytes of room (loop guard); ip: 17 bytes of input (literal guard). */
    __unsafe {
        LZ4_memcpy(__unsafe_forge_bidi_indexable(BYTE*, __unsafe_forge_single(BYTE*, op), 16),
                   __unsafe_forge_bidi_indexable(const BYTE*, __unsafe_forge_single(const BYTE*, ip), 16), 16);
    }
}
LZ4_FORCE_INLINE void LZ4_fastCopy18(BYTE* __bidi_indexable op, const BYTE* __bidi_indexable match)
{
    /* op: 18 bytes of room (loop guard, ml <= 18); match <= op, so its top is
       inside the output too. Its bottom is checked here: in 64 KB prefix mode
       it is otherwise only the caller's promise. */
#if defined(__SAFE_C__)
    if (match < (const BYTE*)__ptr_lower_bound(match)) __builtin_trap();
#endif
    __unsafe {
        BYTE* __unsafe_indexable d = op;
        const BYTE* __unsafe_indexable s = match;
        LZ4_memcpy(__unsafe_forge_bidi_indexable(BYTE*, d, 8), __unsafe_forge_bidi_indexable(const BYTE*, s, 8), 8);
        LZ4_memcpy(__unsafe_forge_bidi_indexable(BYTE*, d + 8, 8), __unsafe_forge_bidi_indexable(const BYTE*, s + 8, 8), 8);
        LZ4_memcpy(__unsafe_forge_bidi_indexable(BYTE*, d + 16, 2), __unsafe_forge_bidi_indexable(const BYTE*, s + 16, 2), 2);
    }
}
LZ4_FORCE_INLINE U16 LZ4_fastReadLE16(const BYTE* __bidi_indexable ip)
{
    /* At most 14 literals were consumed since the `ip <= iend - 17` check. */
    U16 v;
    __unsafe {
        const BYTE* __unsafe_indexable s = ip;
        LZ4_memcpy(&v, __unsafe_forge_bidi_indexable(const BYTE*, s, 2), 2);
    }
    return LZ4_isLittleEndian() ? v : (U16)((v << 8) | (v >> 8));
}

/* LZ4_memcpy_using_offset()  presumes :
 * - dstEnd >= dstPtr + MINMATCH
 * - there is at least 12 bytes available to write after dstEnd */
LZ4_AUDITED LZ4_FORCE_INLINE void
LZ4_memcpy_using_offset(BYTE* __bidi_indexable dstPtr, const BYTE* __bidi_indexable srcPtr, BYTE* __bidi_indexable dstEnd, const size_t offset)
{
    BYTE v[8];

    assert(dstEnd >= dstPtr + MINMATCH);

    switch(offset) {
    case 1:
        MEM_INIT(v, *srcPtr, 8);
        break;
    case 2:
        LZ4_memcpy(v, srcPtr, 2);
        LZ4_memcpy(&v[2], srcPtr, 2);
#if defined(_MSC_VER) && (_MSC_VER <= 1937) /* MSVC 2022 ver 17.7 or earlier */
#  pragma warning(push)
#  pragma warning(disable : 6385) /* warning C6385: Reading invalid data from 'v'. */
#endif
        LZ4_memcpy(&v[4], v, 4);
#if defined(_MSC_VER) && (_MSC_VER <= 1937) /* MSVC 2022 ver 17.7 or earlier */
#  pragma warning(pop)
#endif
        break;
    case 4:
        LZ4_memcpy(v, srcPtr, 4);
        LZ4_memcpy(&v[4], srcPtr, 4);
        break;
    default:
        LZ4_memcpy_using_offset_base(dstPtr, srcPtr, dstEnd, offset);
        return;
    }

    LZ4_memcpy(dstPtr, v, 8);
    dstPtr += 8;
    while (dstPtr < dstEnd) {
        LZ4_memcpy(dstPtr, v, 8);
        dstPtr += 8;
    }
}
#endif


/*-************************************
*  Common functions
**************************************/
static unsigned LZ4_NbCommonBytes (reg_t val)
{
    assert(val != 0);
    if (LZ4_isLittleEndian()) {
        if (sizeof(val) == 8) {
#       if defined(_MSC_VER) && (_MSC_VER >= 1800) && (defined(_M_AMD64) && !defined(_M_ARM64EC)) && !defined(LZ4_FORCE_SW_BITCOUNT)
/*-*************************************************************************************************
* ARM64EC is a Microsoft-designed ARM64 ABI compatible with AMD64 applications on ARM64 Windows 11.
* The ARM64EC ABI does not support AVX/AVX2/AVX512 instructions, nor their relevant intrinsics
* including _tzcnt_u64. Therefore, we need to neuter the _tzcnt_u64 code path for ARM64EC.
****************************************************************************************************/
#         if defined(__clang__) && (__clang_major__ < 10)
            /* Avoid undefined clang-cl intrinsics issue.
             * See https://github.com/lz4/lz4/pull/1017 for details. */
            return (unsigned)__builtin_ia32_tzcnt_u64(val) >> 3;
#         else
            /* x64 CPUS without BMI support interpret `TZCNT` as `REP BSF` */
            return (unsigned)_tzcnt_u64(val) >> 3;
#         endif
#       elif defined(_MSC_VER) && defined(_WIN64) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r = 0;
            _BitScanForward64(&r, (U64)val);
            return (unsigned)r >> 3;
#       elif (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                                        !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_ctzll((U64)val) >> 3;
#       else
            const U64 m = 0x0101010101010101ULL;
            val ^= val - 1;
            return (unsigned)(((U64)((val & (m - 1)) * m)) >> 56);
#       endif
        } else /* 32 bits */ {
#       if defined(_MSC_VER) && (_MSC_VER >= 1400) && !defined(LZ4_FORCE_SW_BITCOUNT)
            unsigned long r;
            _BitScanForward(&r, (U32)val);
            return (unsigned)r >> 3;
#       elif (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                        !defined(__TINYC__) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_ctz((U32)val) >> 3;
#       else
            const U32 m = 0x01010101;
            return (unsigned)((((val - 1) ^ val) & (m - 1)) * m) >> 24;
#       endif
        }
    } else   /* Big Endian CPU */ {
        if (sizeof(val)==8) {
#       if (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                        !defined(__TINYC__) && !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_clzll((U64)val) >> 3;
#       else
#if 1
            /* this method is probably faster,
             * but adds a 128 bytes lookup table */
            static const unsigned char ctz7_tab[128] = {
                7, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                6, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                5, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
                4, 0, 1, 0, 2, 0, 1, 0, 3, 0, 1, 0, 2, 0, 1, 0,
            };
            U64 const mask = 0x0101010101010101ULL;
            U64 const t = (((val >> 8) - mask) | val) & mask;
            return ctz7_tab[(t * 0x0080402010080402ULL) >> 57];
#else
            /* this method doesn't consume memory space like the previous one,
             * but it contains several branches,
             * that may end up slowing execution */
            static const U32 by32 = sizeof(val)*4;  /* 32 on 64 bits (goal), 16 on 32 bits.
            Just to avoid some static analyzer complaining about shift by 32 on 32-bits target.
            Note that this code path is never triggered in 32-bits mode. */
            unsigned r;
            if (!(val>>by32)) { r=4; } else { r=0; val>>=by32; }
            if (!(val>>16)) { r+=2; val>>=8; } else { val>>=24; }
            r += (!val);
            return r;
#endif
#       endif
        } else /* 32 bits */ {
#       if (defined(__clang__) || (defined(__GNUC__) && ((__GNUC__ > 3) || \
                            ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 4))))) && \
                                        !defined(LZ4_FORCE_SW_BITCOUNT)
            return (unsigned)__builtin_clz((U32)val) >> 3;
#       else
            val >>= 8;
            val = ((((val + 0x00FFFF00) | 0x00FFFFFF) + val) |
              (val + 0x00FF0000)) >> 24;
            return (unsigned)val ^ 3;
#       endif
        }
    }
}


#define STEPSIZE sizeof(reg_t)
LZ4_AUDITED LZ4_FORCE_INLINE
unsigned LZ4_count(const BYTE* __bidi_indexable pIn, const BYTE* __bidi_indexable pMatch, const BYTE* __bidi_indexable pInLimit)
{
    const BYTE* const pStart = pIn;

    if (likely(pIn < pInLimit-(STEPSIZE-1))) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) {
            pIn+=STEPSIZE; pMatch+=STEPSIZE;
        } else {
            return LZ4_NbCommonBytes(diff);
    }   }

    while (likely(pIn < pInLimit-(STEPSIZE-1))) {
        reg_t const diff = LZ4_read_ARCH(pMatch) ^ LZ4_read_ARCH(pIn);
        if (!diff) { pIn+=STEPSIZE; pMatch+=STEPSIZE; continue; }
        pIn += LZ4_NbCommonBytes(diff);
        return (unsigned)(pIn - pStart);
    }

    if ((STEPSIZE==8) && (pIn<(pInLimit-3)) && (LZ4_read32(pMatch) == LZ4_read32(pIn))) { pIn+=4; pMatch+=4; }
    if ((pIn<(pInLimit-1)) && (LZ4_read16(pMatch) == LZ4_read16(pIn))) { pIn+=2; pMatch+=2; }
    if ((pIn<pInLimit) && (*pMatch == *pIn)) pIn++;
    return (unsigned)(pIn - pStart);
}


#ifndef LZ4_COMMONDEFS_ONLY
/*-************************************
*  Local Constants
**************************************/
static const int LZ4_64Klimit = ((64 KB) + (MFLIMIT-1));
static const U32 LZ4_skipTrigger = 6;  /* Increase this value ==> compression run slower on incompressible data */


/*-************************************
*  Local Structures and types
**************************************/
typedef enum { clearedTable = 0, byPtr, byU32, byU16 } tableType_t;

/**
 * This enum distinguishes several different modes of accessing previous
 * content in the stream.
 *
 * - noDict        : There is no preceding content.
 * - withPrefix64k : Table entries up to ctx->dictSize before the current blob
 *                   blob being compressed are valid and refer to the preceding
 *                   content (of length ctx->dictSize), which is available
 *                   contiguously preceding in memory the content currently
 *                   being compressed.
 * - usingExtDict  : Like withPrefix64k, but the preceding content is somewhere
 *                   else in memory, starting at ctx->dictionary with length
 *                   ctx->dictSize.
 * - usingDictCtx  : Everything concerning the preceding content is
 *                   in a separate context, pointed to by ctx->dictCtx.
 *                   ctx->dictionary, ctx->dictSize, and table entries
 *                   in the current context that refer to positions
 *                   preceding the beginning of the current compression are
 *                   ignored. Instead, ctx->dictCtx->dictionary and ctx->dictCtx
 *                   ->dictSize describe the location and size of the preceding
 *                   content, and matches are found by looking in the ctx
 *                   ->dictCtx->hashTable.
 */
typedef enum { noDict = 0, withPrefix64k, usingExtDict, usingDictCtx } dict_directive;
typedef enum { noDictIssue = 0, dictSmall } dictIssue_directive;


/*-************************************
*  Local Utils
**************************************/
int LZ4_versionNumber (void) { return LZ4_VERSION_NUMBER; }
const char* LZ4_versionString(void) { return LZ4_VERSION_STRING; }
int LZ4_compressBound(int isize)  { return LZ4_COMPRESSBOUND(isize); }
int LZ4_sizeofState(void) { return sizeof(LZ4_stream_t); }


/*-****************************************
*  Internal Definitions, used only in Tests
*******************************************/
#if defined (__cplusplus)
extern "C" {
#endif


LZ4LIB_STATIC_API int LZ4_decompress_safe_forceExtDict(const char* __counted_by(compressedSize) source, char* __counted_by(maxOutputSize) dest,
                                                       int compressedSize, int maxOutputSize,
                                                       const char* _Nullable __counted_by(dictSize) dictStart, size_t dictSize);
LZ4LIB_STATIC_API int LZ4_decompress_safe_partial_forceExtDict(const char* __counted_by(compressedSize) source, char* __counted_by(dstCapacity) dest,
                                                       int compressedSize, int targetOutputSize, int dstCapacity,
                                                       const char* _Nullable __counted_by(dictSize) dictStart, size_t dictSize);
#if defined (__cplusplus)
}
#endif

/* Safe C port: the obsolete entry points (LZ4_compress(), LZ4_uncompress(), LZ4_create(),
 * LZ4_slideInputBuffer(), LZ4_decompress_*_withPrefix64k(), ...), removed from lz4.h in
 * v1.11.0 but still exported, are removed. */


/*-******************************
*  Compression functions
********************************/
LZ4_FORCE_INLINE U32 LZ4_hash4(U32 sequence, tableType_t const tableType)
{
    const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG+1 : LZ4_HASHLOG;
    return ((sequence * 2654435761U) >> ((MINMATCH*8)-hashLog));
}

LZ4_FORCE_INLINE U32 LZ4_hash5(U64 sequence, tableType_t const tableType)
{
    const U32 hashLog = (tableType == byU16) ? LZ4_HASHLOG+1 : LZ4_HASHLOG;
    if (LZ4_isLittleEndian()) {
        const U64 prime5bytes = 889523592379ULL;
        return (U32)(((sequence << 24) * prime5bytes) >> (64 - hashLog));
    } else {
        const U64 prime8bytes = 11400714785074694791ULL;
        return (U32)(((sequence >> 24) * prime8bytes) >> (64 - hashLog));
    }
}

LZ4_FORCE_INLINE U32 LZ4_hashPosition(const BYTE* __bidi_indexable const p, tableType_t const tableType)
{
    if ((sizeof(reg_t)==8) && (tableType != byU16)) return LZ4_hash5(LZ4_read_ARCH(p), tableType);

#ifdef LZ4_STATIC_LINKING_ONLY_ENDIANNESS_INDEPENDENT_OUTPUT
    return LZ4_hash4(LZ4_readLE32(p), tableType);
#else
    return LZ4_hash4(LZ4_read32(p), tableType);
#endif
}

/* Safe C port: the tables are the U32 array of LZ4_stream_t_internal (byU16 views it as
 * U16), so they take that array instead of `void*`. byPtr mode (32-bit targets only:
 * the table held pointers) is removed; 32-bit targets use byU32. */
LZ4_AUDITED LZ4_FORCE_INLINE void LZ4_clearHash(U32 h, U32* __bidi_indexable tableBase, tableType_t const tableType)
{
    switch (tableType)
    {
    default: /* fallthrough */
    case clearedTable: /* fallthrough */
    case byPtr: { /* illegal! */ assert(0); return; }
    case byU32: { U32* hashTable = tableBase; hashTable[h] = 0; return; }
    case byU16: { U16* hashTable = (U16*) tableBase; hashTable[h] = 0; return; }
    }
}

LZ4_AUDITED LZ4_FORCE_INLINE void LZ4_putIndexOnHash(U32 idx, U32 h, U32* __bidi_indexable tableBase, tableType_t const tableType)
{
    switch (tableType)
    {
    default: /* fallthrough */
    case clearedTable: /* fallthrough */
    case byPtr: { /* illegal! */ assert(0); return; }
    case byU32: { U32* hashTable = tableBase; hashTable[h] = idx; return; }
    case byU16: { U16* hashTable = (U16*) tableBase; assert(idx < 65536); hashTable[h] = (U16)idx; return; }
    }
}

/* LZ4_putPosition() :
 * register position p by index relative to base (byU32, byU16) */
LZ4_FORCE_INLINE void LZ4_putPosition(const BYTE* __bidi_indexable p, U32* __bidi_indexable tableBase, tableType_t tableType, const BYTE* __bidi_indexable base)
{
    U32 const h = LZ4_hashPosition(p, tableType);
    LZ4_putIndexOnHash((U32)(p - base), h, tableBase, tableType);
}

/* LZ4_getIndexOnHash() :
 * Index of match position registered in hash table.
 * hash position must be calculated by using base+index, or dictBase+index.
 * Assumption 1 : only valid if tableType == byU32 or byU16.
 * Assumption 2 : h is presumed valid (within limits of hash table)
 */
LZ4_AUDITED LZ4_FORCE_INLINE U32 LZ4_getIndexOnHash(U32 h, const U32* __bidi_indexable tableBase, tableType_t tableType)
{
    LZ4_STATIC_ASSERT(LZ4_MEMORY_USAGE > 2);
    if (tableType == byU32) {
        const U32* const hashTable = tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-2)));
        return hashTable[h];
    }
    if (tableType == byU16) {
        const U16* const hashTable = (const U16*) tableBase;
        assert(h < (1U << (LZ4_MEMORY_USAGE-1)));
        return hashTable[h];
    }
    assert(0); return 0;  /* forbidden case */
}

/* beyond this input size (1 KB by default),
 * clearing the table is faster than rejecting its stale entries */
#define LZ4_CLEAR_TABLE_THRESHOLD (LZ4_HASHTABLESIZE / 16)

LZ4_FORCE_INLINE void
LZ4_prepareTable(LZ4_stream_t_internal* const cctx,
           const int inputSize,
           const tableType_t tableType) {
    /* If the table hasn't been used, it's guaranteed to be zeroed out, and is
     * therefore safe to use no matter what mode we're in. Otherwise, we figure
     * out if it's safe to leave as is or whether it needs to be reset.
     */
    if ((tableType_t)cctx->tableType != clearedTable) {
        assert(inputSize >= 0);
        if ((tableType_t)cctx->tableType != tableType
          || ((tableType == byU16) && cctx->currentOffset + (unsigned)inputSize >= 0xFFFFU)
          || ((tableType == byU32) && cctx->currentOffset > 1 GB)
          || tableType == byPtr
          || inputSize >= LZ4_CLEAR_TABLE_THRESHOLD)
        {
            DEBUGLOG(4, "LZ4_prepareTable: Resetting table in %p", (void*)cctx);
            MEM_INIT(cctx->hashTable, 0, LZ4_HASHTABLESIZE);
            cctx->currentOffset = 0;
            cctx->tableType = (U32)clearedTable;
        } else {
            DEBUGLOG(4, "LZ4_prepareTable: Re-use hash table (no reset)");
        }
    }

    /* Adding a gap, so all previous entries are > LZ4_DISTANCE_MAX back,
     * is faster than compressing without a gap.
     * However, compressing with currentOffset == 0 is faster still,
     * so we preserve that case.
     */
    if (cctx->currentOffset != 0 && tableType == byU32) {
        DEBUGLOG(5, "LZ4_prepareTable: adding 64KB to currentOffset");
        cctx->currentOffset += 64 KB;
    }

    /* Finally, clear history */
    cctx->dictCtx = NULL;
    cctx->dictionary = NULL;
    cctx->dictSize = 0;
}

/* LZ4_match8() : the 8 bytes at p and q are equal */
LZ4_FORCE_INLINE int LZ4_match8(const BYTE* __bidi_indexable p, const BYTE* __bidi_indexable q)
{
    if (sizeof(reg_t) == 8) return LZ4_read_ARCH(p) == LZ4_read_ARCH(q);
    return (LZ4_read32(p) == LZ4_read32(q))
        && (LZ4_read32(p + 4) == LZ4_read32(q + 4));
}

/* state of an extDict compression which resumes in prefix mode (see LZ4_compress_generic()).
 * Safe C port: positions are offsets from source and dest, not pointers: a struct of
 * borrows (a _View) cannot be filled in through the `suspend` pointer. */
typedef struct {
    size_t ipPos;   /* end of a match : resumes at the test of the next position */
    size_t opPos;
    U32 startIndex;
} LZ4_resume_t;

/** LZ4_compress_generic_validated() :
 *  inlined, to ensure branches are decided at compilation time.
 *  The following conditions are presumed already validated:
 *  - source != NULL
 *  - inputSize > 0
 *  Safe C port: the history arrives as parameters with bounds, not through the
 *  stream's unchecked fields: `dictionary` (dictSize bytes; usingExtDict, usingDictCtx),
 *  and `dictCtx` (usingDictCtx; else any stream, unused). In withPrefix64k mode the
 *  lower bound of `source` covers the dictSize bytes before it.
 */
LZ4_AUDITED LZ4_FORCE_INLINE int LZ4_compress_generic_validated(
                 LZ4_stream_t_internal* const cctx,
                 const char* __bidi_indexable const source,
                 char* __bidi_indexable const dest,
                 const int inputSize,
                 int* _Nullable inputConsumed, /* only written when outputDirective == fillOutput */
                 const int maxOutputSize,
                 const limitedOutput_directive outputDirective,
                 const tableType_t tableType,
                 const dict_directive dictDirective,
                 const dictIssue_directive dictIssue,
                 const int acceleration,
                 LZ4_resume_t* _Nullable const suspend,      /* usingExtDict : stops when the dictionary is out of reach, saving its state */
                 const LZ4_resume_t* _Nullable const resume, /* withPrefix64k : resumes such a compression */
                 const LZ4_stream_t_internal* const dictCtx,
                 const BYTE* __bidi_indexable const dictionary,
                 const U32 dictSize)
{
    int result;
    const BYTE* ip = (const BYTE*)source + (resume ? resume->ipPos : 0);

    U32 const startIndex = resume ? resume->startIndex : cctx->currentOffset;
    const BYTE* base = (const BYTE*)source - startIndex;
    const BYTE* lowLimit;

    const U32 dictDelta =
        (dictDirective == usingDictCtx) ? startIndex - dictCtx->currentOffset : 0;   /* make indexes in dictCtx comparable with indexes in current context */

    int const maybe_extMem = (dictDirective == usingExtDict) || (dictDirective == usingDictCtx);
    U32 const prefixIdxLimit = startIndex - dictSize;   /* used when dictDirective == dictSmall */
    const BYTE* const dictEnd = dictionary + dictSize;
    const BYTE* anchor = ip;
    const BYTE* const iend = (const BYTE*)source + inputSize;
    const BYTE* mflimitPlusOne;   /* set after the size check : would precede a small input */
    const BYTE* matchlimit;
    const BYTE* seqLimit;   /* suspend : mflimitPlusOne, or earlier, where the dictionary gets out of reach */

    /* the dictCtx currentOffset is indexed on the start of the dictionary,
     * while a dictionary in the current context precedes the currentOffset */
    const BYTE* dictBase = (dictDirective == usingDictCtx) ?
                            dictionary + dictSize - dictCtx->currentOffset :
                            dictionary + dictSize - startIndex;

    BYTE* op = (BYTE*)dest + (resume ? resume->opPos : 0);
    BYTE* const olimit = (BYTE*)dest + maxOutputSize;

    U32 offset = 0;
    U32 forwardH;
    const BYTE* filledIp = ip;   /* fillOutput only ; set by catch up (resume : never with fillOutput) */

    DEBUGLOG(5, "LZ4_compress_generic_validated: srcSize=%i, tableType=%u", inputSize, tableType);
    if (tableType == byU16) assert(inputSize<LZ4_64Klimit);  /* Size too large (not within 64K limit) */
    /* If init conditions are not met, we don't have to mark stream
     * as having dirty context, since no action was taken yet */
    if (outputDirective == fillOutput && maxOutputSize < 1) { return 0; } /* Impossible to store anything */
    assert(acceleration >= 1);

    /* resume : candidates are within source (see LZ4_compress_generic()) */
    lowLimit = (const BYTE*)source - ((dictDirective == withPrefix64k) && !resume ? dictSize : 0);

    if (!resume) {   /* resume : context state already updated */
        /* Update context state */
        if (dictDirective == usingDictCtx) {
            /* Subsequent linked blocks can't use the dictionary. */
            /* Instead, they use the block we just compressed. */
            cctx->dictCtx = NULL;
            cctx->dictSize = (U32)inputSize;
        } else {
            cctx->dictSize += (U32)inputSize;
        }
        cctx->currentOffset += (U32)inputSize;
        cctx->tableType = (U32)tableType;

        if (inputSize<LZ4_minLength) goto _last_literals;        /* Input too small, no compression (all literals) */
    }
    mflimitPlusOne = iend - MFLIMIT + 1;
    matchlimit = iend - LASTLITERALS;
    seqLimit = mflimitPlusOne;
    if ((suspend != NULL) && (inputSize > LZ4_DISTANCE_MAX + MFLIMIT - 1))   /* source + LZ4_DISTANCE_MAX < mflimitPlusOne */
        seqLimit = (const BYTE*)source + LZ4_DISTANCE_MAX;

    if (resume) goto _resume;

    /* First Byte */
    LZ4_putPosition(ip, cctx->hashTable, tableType, base);
    ip++; forwardH = LZ4_hashPosition(ip, tableType);

    /* Main Loop */
    for ( ; ; ) {
        const BYTE* match;
        BYTE* token;

        /* Find a match */
        {   /* byU32, byU16 */

            const BYTE* forwardIp = ip;
            int step = 1;
            int searchMatchNb = acceleration << LZ4_skipTrigger;
            do {
                U32 const h = forwardH;
                U32 const curIndex = (U32)(forwardIp - base);
                U32 matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
                assert(matchIndex <= curIndex);
                assert(forwardIp - base < (ptrdiff_t)(2 GB - 1));
                ip = forwardIp;
                forwardIp += step;
                step = (searchMatchNb++ >> LZ4_skipTrigger);

                if (unlikely(forwardIp > mflimitPlusOne)) goto _last_literals;
                assert(ip < mflimitPlusOne);

                if (dictDirective == usingDictCtx) {
                    if (matchIndex < startIndex) {
                        /* there was no match, try the dictionary */
                        assert(tableType == byU32);
                        matchIndex = LZ4_getIndexOnHash(h, dictCtx->hashTable, byU32);
                        match = dictBase + matchIndex;
                        matchIndex += dictDelta;   /* make dictCtx index comparable with current context */
                        lowLimit = dictionary;
                    } else {
                        match = base + matchIndex;
                        lowLimit = (const BYTE*)source;
                    }
                } else if (dictDirective == usingExtDict) {
                    /* select without branch : dictionary and prefix candidates alternate unpredictably.
                     * relative to dictEnd or source, within their buffers (base can be NULL) */
                    ptrdiff_t const relIdx = (ptrdiff_t)matchIndex - (ptrdiff_t)startIndex;   /* < 0 : within dictionary */
                    match = ((relIdx < 0) ? dictEnd : (const BYTE*)source) + relIdx;
                } else {   /* single continuous memory segment */
                    match = base + matchIndex;
                }
                forwardH = LZ4_hashPosition(forwardIp, tableType);
                LZ4_putIndexOnHash(curIndex, h, cctx->hashTable, tableType);

                DEBUGLOG(7, "candidate at pos=%u  (offset=%u \n", matchIndex, curIndex - matchIndex);
                if ((dictIssue == dictSmall) && (matchIndex < prefixIdxLimit)) { continue; }    /* match outside of valid area */
                assert(matchIndex < curIndex);
                /* noDict : the candidate is within input, so it is compared before checking its distance :
                 * this order is faster (better branch prediction) */
                assert((dictDirective != noDict) || (match >= (const BYTE*)source));
                if ( (dictDirective != noDict)
                  && ((tableType != byU16) || (LZ4_DISTANCE_MAX < LZ4_DISTANCE_ABSOLUTE_MAX))
                  && (matchIndex+LZ4_DISTANCE_MAX < curIndex)) {
                    continue;
                } /* too far */

                if (LZ4_read32(match) == LZ4_read32(ip)) {
                    if ( (dictDirective == noDict)
                      && ((tableType != byU16) || (LZ4_DISTANCE_MAX < LZ4_DISTANCE_ABSOLUTE_MAX))
                      && (matchIndex+LZ4_DISTANCE_MAX < curIndex)) {
                        continue;
                    } /* too far */
                    assert((curIndex - matchIndex) <= LZ4_DISTANCE_MAX);  /* match now expected within distance */
                    if (maybe_extMem) offset = curIndex - matchIndex;
                    if (dictDirective == usingExtDict)   /* lowLimit : only needed once a match is found */
                        lowLimit = (matchIndex < startIndex) ? dictionary : (const BYTE*)source;
                    break;   /* match found */
                }

            } while(1);
        }

        /* Catch up */
        filledIp = ip;
        assert(ip > anchor); /* this is always true as ip has been advanced before entering the main loop */
        if ((match > lowLimit) && unlikely(ip[-1] == match[-1])) {
            do { ip--; match--; } while (((ip > anchor) & (match > lowLimit)) && (unlikely(ip[-1] == match[-1])));
        }

        /* Encode Literals */
        {   unsigned const litLength = (unsigned)(ip - anchor);
            token = op++;
            if ((outputDirective == limitedOutput) &&  /* Check output buffer overflow */
                (unlikely(op + litLength + (2 + 1 + LASTLITERALS) + (litLength/255) > olimit)) ) {
                return 0;   /* cannot compress within `dst` budget. Stored indexes in hash table are nonetheless fine */
            }
            if ((outputDirective == fillOutput) &&
                (unlikely(op + (litLength+240)/255 /* litlen */ + litLength /* literals */ + 2 /* offset */ + 1 /* token */ + MFLIMIT - MINMATCH /* min last literals so last match is <= end - MFLIMIT */ > olimit))) {
                op--;
                goto _last_literals;
            }
            if (litLength >= RUN_MASK) {
                unsigned len = litLength - RUN_MASK;
                *token = (RUN_MASK<<ML_BITS);
                for(; len >= 255 ; len-=255) *op++ = 255;
                *op++ = (BYTE)len;
            }
            else *token = (BYTE)(litLength<<ML_BITS);

            /* Copy Literals */
            LZ4_wildCopy8(op, anchor, op+litLength);
            op+=litLength;
            DEBUGLOG(6, "seq.start:%i, literals=%u, match.start:%i",
                        (int)(anchor-(const BYTE*)source), litLength, (int)(ip-(const BYTE*)source));
        }

_next_match:
        /* at this stage, the following variables must be correctly set :
         * - ip : at start of LZ operation
         * - match : at start of previous pattern occurrence; can be within current prefix, or within extDict
         * - offset : if maybe_ext_memSegment==1 (constant)
         * - lowLimit : must be == dictionary to mean "match is within extDict"; must be == source otherwise
         * - token and *token : position to write 4-bits for match length; higher 4-bits for literal length supposed already written
         */

        if ((outputDirective == fillOutput) &&
            (op + 2 /* offset */ + 1 /* token */ + MFLIMIT - MINMATCH /* min last literals so last match is <= end - MFLIMIT */ > olimit)) {
            /* the match was too close to the end, rewind and go to last literals */
            op = token;
            goto _last_literals;
        }

        /* Encode Offset */
        if (maybe_extMem) {   /* static test */
            DEBUGLOG(6, "             with offset=%u  (ext if > %i)", offset, (int)(ip - (const BYTE*)source));
            assert(offset <= LZ4_DISTANCE_MAX && offset > 0);
            LZ4_writeLE16(op, (U16)offset); op+=2;
        } else  {
            DEBUGLOG(6, "             with offset=%u  (same segment)", (U32)(ip - match));
            assert(ip-match <= LZ4_DISTANCE_MAX);
            LZ4_writeLE16(op, (U16)(ip - match)); op+=2;
        }

        /* Encode MatchLength */
        {   unsigned matchCode;

            if ( (dictDirective==usingExtDict || dictDirective==usingDictCtx)
              && (lowLimit==dictionary) /* match within extDict */ ) {
                const BYTE* limit = ip + (dictEnd-match);
                assert(dictEnd > match);
                if (limit > matchlimit) limit = matchlimit;
                matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, limit);
                ip += (size_t)matchCode + MINMATCH;
                if (ip==limit) {
                    unsigned const more = LZ4_count(limit, (const BYTE*)source, matchlimit);
                    matchCode += more;
                    ip += more;
                }
                DEBUGLOG(6, "             with matchLength=%u starting in extDict", matchCode+MINMATCH);
            } else {
                matchCode = LZ4_count(ip+MINMATCH, match+MINMATCH, matchlimit);
                ip += (size_t)matchCode + MINMATCH;
                DEBUGLOG(6, "             with matchLength=%u", matchCode+MINMATCH);
            }

            if ((outputDirective) &&    /* Check output buffer overflow */
                (unlikely(op + (1 + LASTLITERALS) + (matchCode+240)/255 > olimit)) ) {
                if (outputDirective == fillOutput) {
                    /* Match description too long : reduce it */
                    U32 newMatchCode = 15 /* in token */ - 1 /* to avoid needing a zero byte */ + ((U32)(olimit - op) - 1 - LASTLITERALS) * 255;
                    ip -= matchCode - newMatchCode;
                    assert(newMatchCode < matchCode);
                    matchCode = newMatchCode;
                    if (unlikely(ip <= filledIp)) {
                        /* We have already filled up to filledIp so if ip ends up less than filledIp
                         * we have positions in the hash table beyond the current position. This is
                         * a problem if we reuse the hash table. So we have to remove these positions
                         * from the hash table.
                         */
                        const BYTE* ptr;
                        DEBUGLOG(5, "Clearing %u positions", (U32)(filledIp - ip));
                        for (ptr = ip; ptr <= filledIp; ++ptr) {
                            U32 const h = LZ4_hashPosition(ptr, tableType);
                            LZ4_clearHash(h, cctx->hashTable, tableType);
                        }
                    }
                } else {
                    assert(outputDirective == limitedOutput);
                    return 0;   /* cannot compress within `dst` budget. Stored indexes in hash table are nonetheless fine */
                }
            }
            if (matchCode >= ML_MASK) {
                *token += ML_MASK;
                matchCode -= ML_MASK;
                LZ4_write32(op, 0xFFFFFFFF);
                while (matchCode >= 4*255) {
                    op+=4;
                    LZ4_write32(op, 0xFFFFFFFF);
                    matchCode -= 4*255;
                }
                op += matchCode / 255;
                *op++ = (BYTE)(matchCode % 255);
            } else
                *token += (BYTE)(matchCode);
        }
        /* Ensure we have enough space for the last literals. */
        assert(!(outputDirective == fillOutput && op + 1 + LASTLITERALS > olimit));

        anchor = ip;

        /* Test end of chunk */
        if (ip >= seqLimit) {
            if ((suspend == NULL) || (ip >= mflimitPlusOne)) break;
            /* extDict : from ip, all candidates (and catch up) are beyond reach of the dictionary :
             * stop, to resume in prefix mode, which is faster */
            suspend->ipPos = (size_t)(ip - (const BYTE*)source);
            suspend->opPos = (size_t)(op - (BYTE*)dest);
            suspend->startIndex = startIndex;
            return 0;
        }

_resume:
        /* Fill table, and test next position.
         * First, the position after the previous occurrence of ip-1, if it matches 8+ bytes :
         * it breaks chains of short matches, which the table alone can produce on periodic data. */
        {   /* byU32, byU16 */

            U32 const h = LZ4_hashPosition(ip, tableType);
            U32 const curIndex = (U32)(ip-base);
            U32 const hPrev = LZ4_hashPosition(ip-1, tableType);
            U32 prevIndex = LZ4_getIndexOnHash(hPrev, cctx->hashTable, tableType);
            U32 matchIndex;
            LZ4_putIndexOnHash(curIndex-1, hPrev, cctx->hashTable, tableType);
            if ((dictDirective == usingDictCtx) && (prevIndex < startIndex)) {
                /* no occurrence in the current context : try the dictionary */
                prevIndex = LZ4_getIndexOnHash(hPrev, dictCtx->hashTable, byU32) + dictDelta;
            }
            {   /* used table (small block, see LZ4_prepareTable()) : a stale entry yields the candidate of an empty slot
                 * of a cleared table (0), the position after the first one : the output doesn't depend on the table's history */
                int const usedTable = (dictDirective == noDict) && (dictIssue == dictSmall);
                U32 const ctxIndex = ((usedTable && (prevIndex < startIndex)) ? startIndex : prevIndex) + 1;
                int const inDict = (dictDirective == usingExtDict || dictDirective == usingDictCtx) && (ctxIndex < startIndex);
                if ( ((dictIssue==dictSmall) ? (ctxIndex >= prefixIdxLimit) : 1)
                  && (((tableType==byU16) && (LZ4_DISTANCE_MAX == LZ4_DISTANCE_ABSOLUTE_MAX)) ? 1 : (ctxIndex+LZ4_DISTANCE_MAX >= curIndex))
                  && (!inDict || (ctxIndex + 8 <= startIndex))   /* the 8 compared bytes are within the dictionary */
                  && LZ4_match8(inDict ? dictBase + (ctxIndex - dictDelta) : base + ctxIndex, ip) ) {
                    matchIndex = ctxIndex;
                } else {
                    matchIndex = LZ4_getIndexOnHash(h, cctx->hashTable, tableType);
                    if ((dictDirective == usingDictCtx) && (matchIndex < startIndex))
                        matchIndex = LZ4_getIndexOnHash(h, dictCtx->hashTable, byU32) + dictDelta;
                }
            }
            assert(matchIndex < curIndex);
            if (dictDirective == usingExtDict) {   /* select without branch */
                ptrdiff_t const relIdx = (ptrdiff_t)matchIndex - (ptrdiff_t)startIndex;   /* < 0 : within dictionary */
                match = ((relIdx < 0) ? dictEnd : (const BYTE*)source) + relIdx;
            } else if ((dictDirective == usingDictCtx) && (matchIndex < startIndex)) {
                match = dictBase + (matchIndex - dictDelta);
                lowLimit = dictionary;   /* required for match length counter */
            } else {
                match = base + matchIndex;
                if (dictDirective == usingDictCtx)
                    lowLimit = (const BYTE*)source;   /* required for match length counter */
            }
            LZ4_putIndexOnHash(curIndex, h, cctx->hashTable, tableType);
            assert(matchIndex < curIndex);
            if ( ((dictIssue==dictSmall) ? (matchIndex >= prefixIdxLimit) : 1)
              && (((tableType==byU16) && (LZ4_DISTANCE_MAX == LZ4_DISTANCE_ABSOLUTE_MAX)) ? 1 : (matchIndex+LZ4_DISTANCE_MAX >= curIndex))
              && (LZ4_read32(match) == LZ4_read32(ip)) ) {
                token=op++;
                *token=0;
                if (maybe_extMem) offset = curIndex - matchIndex;
                if (dictDirective == usingExtDict)
                    lowLimit = (matchIndex < startIndex) ? dictionary : (const BYTE*)source;   /* required for match length counter */
                DEBUGLOG(6, "seq.start:%i, literals=%u, match.start:%i",
                            (int)(anchor-(const BYTE*)source), 0, (int)(ip-(const BYTE*)source));
                goto _next_match;
            }
        }

        /* Prepare next loop */
        forwardH = LZ4_hashPosition(++ip, tableType);

    }

_last_literals:
    /* Encode Last Literals */
    {   size_t lastRun = (size_t)(iend - anchor);
        if ( (outputDirective) &&  /* Check output buffer overflow */
            (op + lastRun + 1 + ((lastRun+255-RUN_MASK)/255) > olimit)) {
            if (outputDirective == fillOutput) {
                /* adapt lastRun to fill 'dst' */
                assert(olimit >= op);
                lastRun  = (size_t)(olimit-op) - 1/*token*/;
                lastRun -= (lastRun + 256 - RUN_MASK) / 256;  /*additional length tokens*/
            } else {
                assert(outputDirective == limitedOutput);
                return 0;   /* cannot compress within `dst` budget. Stored indexes in hash table are nonetheless fine */
            }
        }
        DEBUGLOG(6, "Final literal run : %i literals", (int)lastRun);
        if (lastRun >= RUN_MASK) {
            size_t accumulator = lastRun - RUN_MASK;
            *op++ = RUN_MASK << ML_BITS;
            for(; accumulator >= 255 ; accumulator-=255) *op++ = 255;
            *op++ = (BYTE) accumulator;
        } else {
            *op++ = (BYTE)(lastRun<<ML_BITS);
        }
        LZ4_memcpy(op, anchor, lastRun);
        ip = anchor + lastRun;
        op += lastRun;
    }

    if ((outputDirective == fillOutput) && (inputConsumed != NULL)) {
        *inputConsumed = (int) (((const char*)ip)-source);
    }
    result = (int)(((char*)op) - dest);
    assert(result > 0);
    DEBUGLOG(5, "LZ4_compress_generic: compressed %i bytes into %i bytes", inputSize, result);
    return result;
}

/* second part of an extDict compression, in prefix mode (see LZ4_compress_generic()).
 * not inlined : a single instance serves all invocations */
LZ4_FORCE_NOINLINE int LZ4_compress_resumeWithPrefix(LZ4_stream_t_internal* cctx,
                 const char* __bidi_indexable src, char* __bidi_indexable dst, int srcSize, int dstCapacity, int acceleration,
                 const LZ4_resume_t* state)
{
    /* resume : candidates are within src, the history is not read */
    return LZ4_compress_generic_validated(cctx, src, dst, srcSize, NULL, dstCapacity, limitedOutput,
                byU32, withPrefix64k, noDictIssue, acceleration, NULL, state,
                cctx, (const BYTE*)src, 0);
}

/* Safe C port: stands in for a missing history (a NULL dictionary of size 0);
 * nothing reads it, as upstream never reads through its NULL. */
static const BYTE LZ4_noHistory[1] = { 0 };

/** LZ4_compress_generic() :
 *  inlined, to ensure branches are decided at compilation time;
 *  takes care of src == (NULL, 0)
 *  and forward the rest to LZ4_compress_generic_validated */
LZ4_FORCE_INLINE int LZ4_compress_generic(
                 LZ4_stream_t_internal* const cctx,
                 const char* _Nullable __bidi_indexable const src,
                 char* __bidi_indexable const dst,
                 const int srcSize,
                 int* _Nullable inputConsumed, /* only written when outputDirective == fillOutput */
                 const int dstCapacity,
                 const limitedOutput_directive outputDirective,
                 const tableType_t tableType,
                 const dict_directive dictDirective,
                 const dictIssue_directive dictIssue,
                 const int acceleration)
{
    const char* source;
    const BYTE* dictionary = LZ4_noHistory;
    const BYTE* _Nullable hist = NULL;
    U32 dictSize = 0;
    const LZ4_stream_t_internal* dictCtx = cctx;

    DEBUGLOG(5, "LZ4_compress_generic: srcSize=%i, dstCapacity=%i",
                srcSize, dstCapacity);

    if ((U32)srcSize > (U32)LZ4_MAX_INPUT_SIZE) { return 0; }  /* Unsupported srcSize, too large (or negative) */
    if (srcSize == 0) {   /* src == NULL supported if srcSize == 0 */
        if (outputDirective != notLimited && dstCapacity <= 0) return 0;  /* no output, can't write anything */
        DEBUGLOG(5, "Generating an empty block");
        assert(outputDirective == notLimited || dstCapacity >= 1);
        dst[0] = 0;
        if (outputDirective == fillOutput) {
            assert (inputConsumed != NULL);
            if (inputConsumed != NULL) *inputConsumed = 0;
        }
        return 1;
    }
    if (src == NULL) return 0;   /* Safe C port: was assert(src != NULL) */
    source = src;

    /* Safe C port: the history (the stream's unchecked pointers) becomes bounded
     * pointers here, once per block. */
    if (dictDirective == usingDictCtx) {
        const LZ4_stream_t_internal* _Nullable ctx = NULL;
        /* LZ4_attach_dictionary() stored a stream that the API requires to stay alive and unmodified while attached */
        __unsafe { ctx = __unsafe_forge_single(const LZ4_stream_t_internal*, cctx->dictCtx); }   // GAP 1: no lifetime-carrying struct behind a pointer (Stream<'a>)
        if (ctx == NULL) return 0;
        dictCtx = ctx;
        dictSize = ctx->dictSize;
        /* ctx->dictionary was loaded by LZ4_loadDict() with ctx->dictSize bytes, which the API requires to stay alive */
        __unsafe { hist = __unsafe_forge_bidi_indexable(const BYTE*, ctx->dictionary, dictSize); }   // GAP 1
    } else if (dictDirective == usingExtDict || dictDirective == withPrefix64k) {
        dictSize = cctx->dictSize;
        /* cctx->dictionary holds the previous block or the loaded dictionary, dictSize bytes, which the streaming API requires to stay alive */
        __unsafe { hist = __unsafe_forge_bidi_indexable(const BYTE*, cctx->dictionary, dictSize); }   // GAP 1
        if (dictDirective == withPrefix64k) {
            /* prefix mode : the history ends where src starts (checked here), within one buffer the
             * API requires to stay alive, so a pointer may cover both */
            __unsafe {   // GAP 1, GAP 3: two adjacent bounded buffers cannot be joined
                if ((const char*)cctx->dictionary + dictSize != src) return 0;
                source = __unsafe_forge_bidi_indexable(const char*, (const char*)cctx->dictionary, (size_t)dictSize + (size_t)srcSize) + dictSize;
            }
        }
    }
    if (hist != NULL) dictionary = hist;

    if ((dictDirective == usingExtDict) && (dictIssue == noDictIssue)) {
        /* beyond LZ4_DISTANCE_MAX, the dictionary is out of reach : the rest of the input is compressed in prefix mode */
        LZ4_resume_t state;
        int result;
        assert(tableType == byU32);
        assert(outputDirective != fillOutput);
        state.ipPos = 0; state.opPos = 0; state.startIndex = 0;
        result = LZ4_compress_generic_validated(cctx, source, dst, srcSize, inputConsumed, dstCapacity, outputDirective,
                    tableType, usingExtDict, noDictIssue, acceleration, &state, NULL, dictCtx, dictionary, dictSize);
        if (state.ipPos == 0) return result;   /* completed (or failed) in extDict mode */
        /* notLimited : dst is presumed large enough for LZ4_compressBound() */
        return LZ4_compress_resumeWithPrefix(cctx, source, dst, srcSize,
                    (outputDirective == notLimited) ? LZ4_COMPRESSBOUND(srcSize) : dstCapacity,
                    acceleration, &state);
    }

    return LZ4_compress_generic_validated(cctx, source, dst, srcSize,
                inputConsumed, /* only written into if outputDirective == fillOutput */
                dstCapacity, outputDirective,
                tableType, dictDirective, dictIssue, acceleration, NULL, NULL,
                dictCtx, dictionary, dictSize);
}


/* values < 1 select the default, values > LZ4_ACCELERATION_MAX are capped */
static int LZ4_clampAcceleration(int acceleration)
{
    if (acceleration < 1) return LZ4_ACCELERATION_DEFAULT;
    if (acceleration > LZ4_ACCELERATION_MAX) return LZ4_ACCELERATION_MAX;
    return acceleration;
}

static size_t LZ4_stream_t_alignment(void)
{
#if LZ4_ALIGN_TEST
    typedef struct { char c; LZ4_stream_t t; } t_a;
    return sizeof(t_a) - sizeof(LZ4_stream_t);
#else
    return 1;  /* effectively disabled */
#endif
}

/* outputDirective must be a compile-time constant
 * Safe C port: inputs too large for byU16 use byU32 (byPtr is removed) */
LZ4_FORCE_INLINE int LZ4_compress_fastReset_generic(
                 LZ4_stream_t_internal* const ctx,
                 const char* _Nullable __bidi_indexable const src,
                 char* __bidi_indexable const dst,
                 const int srcSize,
                 const int dstCapacity,
                 const limitedOutput_directive outputDirective,
                 const int acceleration)
{
    if (srcSize < LZ4_64Klimit) {
        LZ4_prepareTable(ctx, srcSize, byU16);
        if (ctx->currentOffset) {  /* table re-used : stale entries must be rejected */
            return LZ4_compress_generic(ctx, src, dst, srcSize, NULL, dstCapacity, outputDirective, byU16, noDict, dictSmall, acceleration);
        } else {
            return LZ4_compress_generic(ctx, src, dst, srcSize, NULL, dstCapacity, outputDirective, byU16, noDict, noDictIssue, acceleration);
        }
    } else {
        LZ4_prepareTable(ctx, srcSize, byU32);
        return LZ4_compress_generic(ctx, src, dst, srcSize, NULL, dstCapacity, outputDirective, byU32, noDict, noDictIssue, acceleration);
    }
}

/* Safe C port: LZ4_initStream() for a typed state. It sets the fields one by one :
 * a memset of the whole struct writes bytes over its pointers. */
static void LZ4_initStream_internal(LZ4_stream_t_internal* s)
{
    MEM_INIT(s->hashTable, 0, sizeof(s->hashTable));
    s->dictionary = NULL;
    s->dictCtx = NULL;
    s->currentOffset = 0;
    s->tableType = (U32)clearedTable;
    s->dictSize = 0;
}

/* Safe C port: the public entry points take `void* state`; these take the typed state. */
static int LZ4_compress_fast_extState_fastReset_internal(LZ4_stream_t* state, const char* _Nullable __counted_by(srcSize) src, char* __counted_by(dstCapacity) dst, int srcSize, int dstCapacity, int acceleration)
{
    LZ4_stream_t_internal* const ctx = &state->internal_donotuse;
    acceleration = LZ4_clampAcceleration(acceleration);

    if (dstCapacity >= LZ4_compressBound(srcSize)) {
        return LZ4_compress_fastReset_generic(ctx, src, dst, srcSize, 0, notLimited, acceleration);
    } else {
        return LZ4_compress_fastReset_generic(ctx, src, dst, srcSize, dstCapacity, limitedOutput, acceleration);
    }
}

int LZ4_compress_fast_extState(void* __sized_by(LZ4_STREAM_MINSIZE) state, const char* _Nullable __counted_by(inputSize) source, char* __counted_by(maxOutputSize) dest, int inputSize, int maxOutputSize, int acceleration)
{
    /* a freshly initialized state is valid for _fastReset() */
    LZ4_stream_t* _Nullable const s = LZ4_initStream(state, LZ4_STREAM_MINSIZE);
    if (s == NULL) return 0;   /* Safe C port: was assert(s != NULL) (a misaligned state) */
    return LZ4_compress_fast_extState_fastReset_internal(s, source, dest, inputSize, maxOutputSize, acceleration);
}

/**
 * LZ4_compress_fast_extState_fastReset() :
 * A variant of LZ4_compress_fast_extState().
 *
 * Using this variant avoids an expensive initialization step. It is only safe
 * to call if the state buffer is known to be correctly initialized already
 * (see comment in lz4.h on LZ4_resetStream_fast() for a definition of
 * "correctly initialized").
 */
int LZ4_compress_fast_extState_fastReset(void* __sized_by(LZ4_STREAM_MINSIZE) state, const char* _Nullable __counted_by(srcSize) src, char* __counted_by(dstCapacity) dst, int srcSize, int dstCapacity, int acceleration)
{
    LZ4_stream_t* s = NULL;
    if (!LZ4_isAligned(state, LZ4_stream_t_alignment())) return 0;
    /* the bytes are LZ4_STREAM_MINSIZE (the parameter's bounds) and aligned; whatever they hold, the
     * code reads integers, bounds-checked through them, and two unchecked pointers that
     * LZ4_prepareTable() clears before any use */
    __unsafe { s = (LZ4_stream_t*)state; }   // GAP 2: a struct of integers and unchecked pointers counts as pointer-holding
    return LZ4_compress_fast_extState_fastReset_internal(s, src, dst, srcSize, dstCapacity, acceleration);
}


int LZ4_compress_fast(const char* _Nullable __counted_by(srcSize) src, char* __counted_by(dstCapacity) dest, int srcSize, int dstCapacity, int acceleration)
{
    int result;
#if (LZ4_HEAPMODE)
    LZ4_stream_t* const ctxPtr = (LZ4_stream_t*)ALLOC(sizeof(LZ4_stream_t));   /* malloc-calloc always properly aligned */
    if (ctxPtr == NULL) return 0;
#else
    LZ4_stream_t ctx;
    LZ4_stream_t* const ctxPtr = &ctx;
#endif
    LZ4_initStream_internal(&ctxPtr->internal_donotuse);
    result = LZ4_compress_fast_extState_fastReset_internal(ctxPtr, src, dest, srcSize, dstCapacity, acceleration);

#if (LZ4_HEAPMODE)
    FREEMEM(ctxPtr);
#endif
    return result;
}


int LZ4_compress_default(const char* _Nullable __counted_by(srcSize) src, char* __counted_by(dstCapacity) dst, int srcSize, int dstCapacity)
{
    return LZ4_compress_fast(src, dst, srcSize, dstCapacity, 1);
}


/* Note!: This function leaves the stream in an unclean/broken state!
 * It is not safe to subsequently use the same state with a _fastReset() or
 * _continue() call without resetting it. */
static int LZ4_compress_destSize_extState_internal(LZ4_stream_t* state, const char* __counted_by(*srcSizePtr) src, char* __counted_by(targetDstSize) dst, int* srcSizePtr, int targetDstSize, int acceleration)
{
    int const srcSize = *srcSizePtr;
    LZ4_initStream_internal(&state->internal_donotuse);
    acceleration = LZ4_clampAcceleration(acceleration);

    if (targetDstSize >= LZ4_compressBound(srcSize)) {  /* compression success is guaranteed */
        return LZ4_compress_fast_extState_fastReset_internal(state, src, dst, srcSize, targetDstSize, acceleration);
    } else {
        /* Safe C port: *srcSizePtr is the count of src, so the consumed size goes through a local */
        int consumed = srcSize;
        int r;
        if (srcSize < LZ4_64Klimit) {
            r = LZ4_compress_generic(&state->internal_donotuse, src, dst, srcSize, &consumed, targetDstSize, fillOutput, byU16, noDict, noDictIssue, acceleration);
        } else {
            r = LZ4_compress_generic(&state->internal_donotuse, src, dst, srcSize, &consumed, targetDstSize, fillOutput, byU32, noDict, noDictIssue, acceleration);
        }
        *srcSizePtr = consumed;
        return r;
    }
}

int LZ4_compress_destSize_extState(void* __sized_by(LZ4_STREAM_MINSIZE) state, const char* __counted_by(*srcSizePtr) src, char* __counted_by(targetDstSize) dst, int* srcSizePtr, int targetDstSize, int acceleration)
{
    int r;
    LZ4_stream_t* _Nullable const s = LZ4_initStream(state, LZ4_STREAM_MINSIZE);
    if (s == NULL) return 0;   /* Safe C port: a misaligned state */
    r = LZ4_compress_destSize_extState_internal(s, src, dst, srcSizePtr, targetDstSize, acceleration);
    /* clean the state on exit */
    LZ4_initStream_internal(&s->internal_donotuse);
    return r;
}


int LZ4_compress_destSize(const char* __counted_by(*srcSizePtr) src, char* __counted_by(targetDstSize) dst, int* srcSizePtr, int targetDstSize)
{
    int result;
#if (LZ4_HEAPMODE)
    LZ4_stream_t* const ctx = (LZ4_stream_t*)ALLOC(sizeof(LZ4_stream_t));   /* malloc-calloc always properly aligned */
    if (ctx == NULL) return 0;
#else
    LZ4_stream_t ctxBody;
    LZ4_stream_t* const ctx = &ctxBody;
#endif
    result = LZ4_compress_destSize_extState_internal(ctx, src, dst, srcSizePtr, targetDstSize, 1);

#if (LZ4_HEAPMODE)
    FREEMEM(ctx);
#endif
    return result;
}



/*-******************************
*  Streaming functions
********************************/

_Returns_owned LZ4_stream_t* _Nullable LZ4_createStream(void)
{
    LZ4_stream_t* _Nullable const lz4s = (LZ4_stream_t*)ALLOC(sizeof(LZ4_stream_t));
    LZ4_STATIC_ASSERT(sizeof(LZ4_stream_t) >= sizeof(LZ4_stream_t_internal));
    /* Safe C port: LZ4_stream_t lost its char[LZ4_STREAM_MINSIZE] member, which fixed its size */
    LZ4_STATIC_ASSERT((sizeof(void*) != 8) || (sizeof(LZ4_stream_t) == LZ4_STREAM_MINSIZE));
    DEBUGLOG(4, "LZ4_createStream %p", (void*)lz4s);
    if (lz4s == NULL) return NULL;
    LZ4_initStream_internal(&lz4s->internal_donotuse);
    return lz4s;
}

LZ4_stream_t* _Nullable LZ4_initStream (void* _Nullable __sized_by(size) buffer _Lifetimebound, size_t size)
{
    LZ4_stream_t* _Nullable s = NULL;
    DEBUGLOG(5, "LZ4_initStream");
    if (buffer == NULL) { return NULL; }
    if (size < sizeof(LZ4_stream_t)) { return NULL; }
    if (!LZ4_isAligned(buffer, LZ4_stream_t_alignment())) return NULL;
    MEM_INIT(buffer, 0, sizeof(LZ4_stream_t_internal));
    /* the buffer is large enough and aligned (checked above) and now all zero :
     * a valid LZ4_stream_t, whose pointers are NULL */
    __unsafe { s = (LZ4_stream_t*)buffer; }   // GAP 2
    return s;
}


void LZ4_resetStream_fast(LZ4_stream_t* ctx) {
    LZ4_prepareTable(&(ctx->internal_donotuse), 0, byU32);
}

int LZ4_freeStream (LZ4_stream_t* _Owned _Nullable LZ4_stream)
{
    if (!LZ4_stream) return 0;   /* support free on NULL */
    DEBUGLOG(5, "LZ4_freeStream %p", (void*)LZ4_stream);
    FREEMEM(LZ4_stream);
    return (0);
}


typedef enum { _ld_fast, _ld_slow } LoadDict_mode_e;
#define HASH_UNIT sizeof(reg_t)
static int LZ4_loadDict_internal(LZ4_stream_t* LZ4_dict,
                    const char* _Nullable __counted_by(dictSize) dictionary, int dictSize,
                    LoadDict_mode_e _ld)
{
    LZ4_stream_t_internal* const dict = &LZ4_dict->internal_donotuse;
    const tableType_t tableType = byU32;
    const BYTE* p;
    const BYTE* dictEnd;
    const BYTE* dictStart;
    U32 idx32;

    DEBUGLOG(4, "LZ4_loadDict (%i bytes from %p into %p)", dictSize, (void*)dictionary, (void*)LZ4_dict);

    /* It's necessary to reset the context,
     * and not just continue it with prepareTable()
     * to avoid any risk of generating overflowing matchIndex
     * when compressing using this dictionary */
    LZ4_initStream_internal(dict);

    /* We always increment the offset by 64 KB, since, if the dict is longer,
     * we truncate it to the last 64k, and if it's shorter, we still want to
     * advance by a whole window length so we can provide the guarantee that
     * there are only valid offsets in the window, which allows an optimization
     * in LZ4_compress_fast_continue() where it uses noDictIssue even when the
     * dictionary isn't a full 64k. */
    dict->currentOffset += 64 KB;
    /* no longer a cleared table : LZ4_prepareTable() must check it,
     * even when the dictionary is too small to be used */
    dict->tableType = (U32)tableType;

    if (dictSize < (int)HASH_UNIT || dictionary == NULL) {
        return 0;
    }
    p = (const BYTE*)dictionary;
    dictEnd = p + dictSize;

    if ((dictEnd - p) > 64 KB) p = dictEnd - 64 KB;
    dictStart = p;
    dict->dictionary = p;
    dict->dictSize = (U32)(dictEnd - p);
    idx32 = dict->currentOffset - dict->dictSize;

    while (p <= dictEnd-HASH_UNIT) {
        U32 const h = LZ4_hashPosition(p, tableType);
        /* Note: overwriting => favors positions end of dictionary */
        LZ4_putIndexOnHash(idx32, h, dict->hashTable, tableType);
        p+=3; idx32+=3;
    }

    if (_ld == _ld_slow) {
        /* Fill hash table with additional references, to improve compression capability */
        p = dictStart;
        idx32 = dict->currentOffset - dict->dictSize;
        while (p <= dictEnd-HASH_UNIT) {
            U32 const h = LZ4_hashPosition(p, tableType);
            U32 const limit = dict->currentOffset - 64 KB;
            if (LZ4_getIndexOnHash(h, dict->hashTable, tableType) <= limit) {
                /* Note: not overwriting => favors positions beginning of dictionary */
                LZ4_putIndexOnHash(idx32, h, dict->hashTable, tableType);
            }
            p++; idx32++;
        }
    }

    return (int)dict->dictSize;
}

int LZ4_loadDict(LZ4_stream_t* LZ4_dict, const char* _Nullable __counted_by(dictSize) dictionary, int dictSize)
{
    return LZ4_loadDict_internal(LZ4_dict, dictionary, dictSize, _ld_fast);
}

int LZ4_loadDictSlow(LZ4_stream_t* LZ4_dict, const char* _Nullable __counted_by(dictSize) dictionary, int dictSize)
{
    return LZ4_loadDict_internal(LZ4_dict, dictionary, dictSize, _ld_slow);
}

void LZ4_attach_dictionary(LZ4_stream_t* workingStream, const LZ4_stream_t* _Nullable dictionaryStream)
{
    const LZ4_stream_t_internal* _Nullable dictCtx = (dictionaryStream == NULL) ? NULL :
        &(dictionaryStream->internal_donotuse);

    DEBUGLOG(4, "LZ4_attach_dictionary (%p, %p, size %u)",
             (void*)workingStream, (void*)dictionaryStream,
             dictCtx != NULL ? dictCtx->dictSize : 0);

    if (dictCtx != NULL) {
        /* If the current offset is zero, we will never look in the
         * external dictionary context, since there is no value a table
         * entry can take that indicate a miss. In that case, we need
         * to bump the offset to something non-zero.
         */
        if (workingStream->internal_donotuse.currentOffset == 0) {
            workingStream->internal_donotuse.currentOffset = 64 KB;
            /* no longer a cleared table : LZ4_prepareTable() must check it */
            workingStream->internal_donotuse.tableType = (U32)byU32;
        }

        /* Don't actually attach an empty dictionary.
         */
        if (dictCtx->dictSize == 0) {
            dictCtx = NULL;
        }
    }
    workingStream->internal_donotuse.dictCtx = dictCtx;
}

/* Safe C port: the stream's history pointer is unchecked; these read its address (an integer)
 * or move it within the history, which needs no bounds. */
static uptrval LZ4_dictAddr(const LZ4_stream_t_internal* s)
{
    uptrval a = 0;
    __unsafe { a = (uptrval)s->dictionary; }   /* reads the address only */   // GAP 4: an unchecked pointer's address is not plain data
    return a;
}

static int LZ4_hasDictCtx(const LZ4_stream_t_internal* s)
{
    return s->dictCtx != NULL;
}

/* keeps the last s->dictSize bytes of a history that was oldSize bytes */
static void LZ4_dictKeepTail(LZ4_stream_t_internal* s, U32 oldSize)
{
    assert(s->dictSize <= oldSize);
    /* the result stays within the old history (dictSize <= oldSize) */
    __unsafe { s->dictionary = s->dictionary + (oldSize - s->dictSize); }   // GAP 4: wrapping arithmetic on an unchecked pointer
}

static void LZ4_renormDictT(LZ4_stream_t_internal* LZ4_dict, int nextSize)
{
    assert(nextSize >= 0);
    if (LZ4_dict->currentOffset + (unsigned)nextSize > 0x80000000) {   /* potential ptrdiff_t overflow (32-bits mode) */
        /* rescale hash table */
        U32 const delta = LZ4_dict->currentOffset - 64 KB;
        U32 const oldSize = LZ4_dict->dictSize;
        int i;
        DEBUGLOG(4, "LZ4_renormDictT");
        for (i=0; i<LZ4_HASH_SIZE_U32; i++) {
            if (LZ4_dict->hashTable[i] < delta) LZ4_dict->hashTable[i]=0;
            else LZ4_dict->hashTable[i] -= delta;
        }
        LZ4_dict->currentOffset = 64 KB;
        if (LZ4_dict->dictSize > 64 KB) LZ4_dict->dictSize = 64 KB;
        LZ4_dictKeepTail(LZ4_dict, oldSize);
    }
}


/* The history covers neither the full window (64 KB) nor all positions since the start of indexes :
 * the table may contain stale entries, from before the history, which must be rejected (dictSmall). */
static int LZ4_isDictSmall(const LZ4_stream_t_internal* streamPtr)
{
    return (streamPtr->dictSize < 64 KB) && (streamPtr->dictSize < streamPtr->currentOffset);
}

/* from this input size (2 KB by default, at most 4 KB),
 * copying the tables of an attached dictionary is faster than looking into both tables.
 * The copy cost follows the table size. */
#define LZ4_DICTCTX_COPY_THRESHOLD ((LZ4_HASHTABLESIZE / 8 < 4 KB) ? (LZ4_HASHTABLESIZE / 8) : 4 KB)


int LZ4_compress_fast_continue (LZ4_stream_t* LZ4_stream,
                                const char* _Nullable __counted_by(inputSize) source, char* __counted_by(maxOutputSize) dest,
                                int inputSize, int maxOutputSize,
                                int acceleration)
{
    const tableType_t tableType = byU32;
    LZ4_stream_t_internal* const streamPtr = &LZ4_stream->internal_donotuse;
    /* Safe C port: the history is compared by address (as integers) */
    uptrval const srcStart = (uptrval)source;
    uptrval dictEnd;

    DEBUGLOG(5, "LZ4_compress_fast_continue (inputSize=%i, dictSize=%u)", inputSize, streamPtr->dictSize);

    /* new stream (no history) : clear the table beyond LZ4_CLEAR_TABLE_THRESHOLD */
    if ((streamPtr->dictSize == 0) && !LZ4_hasDictCtx(streamPtr) && (inputSize >= LZ4_CLEAR_TABLE_THRESHOLD))
        LZ4_prepareTable(streamPtr, inputSize, byU32);

    LZ4_renormDictT(streamPtr, inputSize);   /* fix index overflow (keeps the end of the history) */
    acceleration = LZ4_clampAcceleration(acceleration);
    dictEnd = streamPtr->dictSize ? LZ4_dictAddr(streamPtr) + streamPtr->dictSize : 0;

    /* invalidate tiny dictionaries */
    if ( (streamPtr->dictSize < 4)     /* tiny dictionary : not enough for a hash */
      && (dictEnd != srcStart)         /* prefix mode */
      && (inputSize > 0)               /* tolerance : don't lose history, in case next invocation would use prefix mode */
      && !LZ4_hasDictCtx(streamPtr)    /* usingDictCtx */
      ) {
        DEBUGLOG(5, "LZ4_compress_fast_continue: dictSize(%u) is too small", streamPtr->dictSize);
        /* remove dictionary existence from history, to employ faster prefix mode */
        streamPtr->dictSize = 0;
        streamPtr->dictionary = (const BYTE*)source;
        dictEnd = srcStart;
    }

    /* Check overlapping input/dictionary space */
    {   uptrval const sourceEnd = srcStart + (uptrval)inputSize;
        if ((sourceEnd > LZ4_dictAddr(streamPtr)) && (srcStart < dictEnd)) {
            U32 const oldSize = streamPtr->dictSize;
            /* only the part of the dictionary beyond input remains */
            streamPtr->dictSize = (sourceEnd < dictEnd) ? (U32)(dictEnd - sourceEnd) : 0;
            if (streamPtr->dictSize > 64 KB) streamPtr->dictSize = 64 KB;
            if (streamPtr->dictSize < 4) streamPtr->dictSize = 0;
            LZ4_dictKeepTail(streamPtr, oldSize);
        }
    }

    /* prefix mode : source data follows dictionary */
    if (dictEnd == srcStart) {
        if (LZ4_isDictSmall(streamPtr))
            return LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, withPrefix64k, dictSmall, acceleration);
        else
            return LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, withPrefix64k, noDictIssue, acceleration);
    }

    /* external dictionary mode */
    {   int result;
        if (LZ4_hasDictCtx(streamPtr)) {
            /* We depend here on the fact that dictCtx'es (produced by
             * LZ4_loadDict) guarantee that their tables contain no references
             * to offsets between dictCtx->currentOffset - 64 KB and
             * dictCtx->currentOffset - dictCtx->dictSize. This makes it safe
             * to use noDictIssue even when the dict isn't a full 64 KB.
             */
            if (inputSize >= LZ4_DICTCTX_COPY_THRESHOLD) {
                /* For compressing large blobs, it is faster to pay the setup
                 * cost to copy the dictionary's tables into the active context,
                 * so that the compression loop is only looking into one table.
                 * Safe C port: a struct copy, not a memcpy over the pointers. */
                /* the attached stream must stay alive while attached (API contract), and is not this one */
                __unsafe { *streamPtr = *streamPtr->dictCtx; }   // GAP 1
                result = LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, usingExtDict, noDictIssue, acceleration);
            } else {
                result = LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, usingDictCtx, noDictIssue, acceleration);
            }
        } else {  /* external dictionary in the current context */
            if (LZ4_isDictSmall(streamPtr)) {
                result = LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, usingExtDict, dictSmall, acceleration);
            } else {
                result = LZ4_compress_generic(streamPtr, source, dest, inputSize, NULL, maxOutputSize, limitedOutput, tableType, usingExtDict, noDictIssue, acceleration);
            }
        }
        streamPtr->dictionary = (const BYTE*)source;
        streamPtr->dictSize = (U32)inputSize;
        return result;
    }
}

/* Safe C port: LZ4_compress_forceExtDict() (a hidden debug entry point for tests, which
 * writes up to LZ4_compressBound(srcSize) bytes into a dst of unknown size) is removed. */


/*! LZ4_saveDict() :
 *  If previously compressed data block is not guaranteed to remain available at its memory location,
 *  save it into a safer place (char* safeBuffer).
 *  Note : no need to call LZ4_loadDict() afterwards, dictionary is immediately usable,
 *         one can therefore call LZ4_compress_fast_continue() right after.
 * @return : saved dictionary size in bytes (necessarily <= dictSize), or 0 if error.
 */
int LZ4_saveDict (LZ4_stream_t* LZ4_dict, char* _Nullable __counted_by(maxDictSize) safeBuffer, int maxDictSize)
{
    LZ4_stream_t_internal* const dict = &LZ4_dict->internal_donotuse;
    int dictSize = maxDictSize;

    DEBUGLOG(5, "LZ4_saveDict : dictSize=%i, safeBuffer=%p", dictSize, (void*)safeBuffer);

    if (dictSize < 0) dictSize = 0;   /* invalid : save nothing, like LZ4_saveDictHC() */
    if ((U32)dictSize > 64 KB) { dictSize = 64 KB; } /* useless to define a dictionary > 64 KB */
    if ((U32)dictSize > dict->dictSize) { dictSize = (int)dict->dictSize; }

    if (safeBuffer == NULL) dictSize = 0;   /* Safe C port: was assert(dictSize == 0) */
    if ((dictSize > 0) && (safeBuffer != NULL)) {
        const BYTE* previous = NULL;
        /* the history is dict->dictSize bytes the streaming API requires to stay alive until saved */
        __unsafe { previous = __unsafe_forge_bidi_indexable(const BYTE*, dict->dictionary, dict->dictSize); }   // GAP 1
        LZ4_memmove(safeBuffer, previous + (dict->dictSize - (U32)dictSize), (size_t)dictSize);
    }

    dict->dictionary = (const BYTE*)safeBuffer;
    dict->dictSize = (U32)dictSize;

    return dictSize;
}



/*-*******************************
 *  Decompression functions
 ********************************/

typedef enum { decode_full_block = 0, partial_decode = 1 } earlyEnd_directive;
typedef enum { complete_input = 0, incomplete_input = 1 } inputEnd_directive;

#undef MIN
#define MIN(a,b)    ( (a) < (b) ? (a) : (b) )




/* Read the variable-length literal or match length.
 *
 * @ipPtr : pointer to input pointer, will be advanced
 * @ilimit : read is forbidden beyond this position (must be within input buffer)
**/
typedef size_t Rvl_t;
static const Rvl_t rvl_error = (Rvl_t)(-1);
LZ4_FORCE_INLINE Rvl_t
read_variable_length(const BYTE* __bidi_indexable* ipPtr, const BYTE* __bidi_indexable ilimit)
{
    Rvl_t s, length = 0;
    assert(ipPtr != NULL);
    assert(*ipPtr !=  NULL);
    assert(ilimit != NULL);
    if (unlikely((*ipPtr) >= ilimit)) {    /* read limit reached */
        return rvl_error;
    }
    s = **ipPtr;
    (*ipPtr)++;
    length += s;
    if (likely(s != 255)) return length;
    do {
        if (unlikely((*ipPtr) >= ilimit)) {    /* read limit reached */
            return rvl_error;
        }
        s = **ipPtr;
        (*ipPtr)++;
        length += s;
        /* accumulator overflow detection (32-bit mode only) */
        if ((sizeof(length) < 8) && unlikely(length > ((Rvl_t)(-1)/2)) ) {
            return rvl_error;
        }
    } while (s == 255);

    return length;
}

/* LZ4_copyMatchExact() :
 * copy a match of length bytes from match to op (match < op, overlap allowed),
 * writing nothing beyond op+length. Invalid offset 0 (match == op) produces 0 bytes.
 * Not inlined : rarely used, its memcpy() calls would burden the decoder's hot loops.
 * @return : output position after the match */
LZ4_FORCE_NOINLINE BYTE* __bidi_indexable
LZ4_copyMatchExact(BYTE* __bidi_indexable op _Lifetimebound, const BYTE* __bidi_indexable match, size_t length)
{
    if (unlikely(match == op)) {   /* invalid offset 0 : clear the match segment, see doc/lz4_Block_format.md */
        MEM_INIT(op, 0, length);
        return op + length;
    }
    /* bytes from match to op repeat with period (op-match) :
     * copy all of them, so the distance doubles at each round.
     * Beyond 2 KB, keep the distance : larger copies of just written data are slower */
    while (length > (size_t)(op - match)) {
        size_t const available = (size_t)(op - match);
        LZ4_memcpy(op, match, available);
        op += available;
        length -= available;
        if (available >= 2048) match += available;
    }
    LZ4_memcpy(op, match, length);
    return op + length;
}

/* LZ4_copyExtDictMatch() :
 * copy a match starting within the external dictionary (match < lowPrefix),
 * possibly stretching into the current block.
 * @return : output position after the match */
LZ4_FORCE_INLINE BYTE* __bidi_indexable
LZ4_copyExtDictMatch(BYTE* __bidi_indexable op _Lifetimebound, const BYTE* __bidi_indexable match, size_t length,
                     const BYTE* __bidi_indexable lowPrefix, const BYTE* __bidi_indexable dictEnd)
{
    if (length <= (size_t)(lowPrefix-match)) {
        /* match fits entirely within external dictionary : just copy */
        LZ4_memmove(op, dictEnd - (lowPrefix-match), length);
        return op + length;
    } else {
        /* match stretches into both external dictionary and current block */
        size_t const copySize = (size_t)(lowPrefix - match);
        LZ4_memcpy(op, dictEnd - copySize, copySize);
        return LZ4_copyMatchExact(op + copySize, lowPrefix, length - copySize);
    }
}

/*! LZ4_decompress_generic() :
 *  This generic decompression function covers all use cases.
 *  It shall be instantiated several times, using different sets of directives.
 *  Note that it is important for performance that this function really get inlined,
 *  in order to remove useless branches during compilation optimization.
 */
LZ4_AUDITED LZ4_FORCE_INLINE int
LZ4_decompress_generic(
                 const char* __bidi_indexable const src,
                 char* __bidi_indexable const dst,
                 int srcSize,
                 int outputSize,         /* dstCapacity, or MIN(targetOutputSize, dstCapacity) when partialDecoding */

                 earlyEnd_directive partialDecoding,  /* full, partial */
                 dict_directive dict,                 /* noDict, withPrefix64k, usingExtDict */
                 const BYTE* __bidi_indexable const lowPrefix,  /* always <= dst, == dst when no prefix */
                 const BYTE* __bidi_indexable const dictStart,  /* only read if dict==usingExtDict ; Safe C port: never NULL */
                 const size_t dictSize,        /* note : = 0 if noDict */
                 inputEnd_directive incompleteInput,  /* incomplete_input : the block goes on beyond srcSize (see LZ4_decompress_incomplete()) */
                 const char* __bidi_indexable* _Nullable srcEndPtr        /* incomplete_input only : receives the end of the decoded sequences */
                 )
{
    if ((src == NULL) || (outputSize < 0)) { return -1; }

    {   const BYTE* ip = (const BYTE*) src;
        const BYTE* const iend = ip + srcSize;

        BYTE* op = (BYTE*) dst;
        BYTE* const oend = op + outputSize;

        const BYTE* const dictEnd = dictStart + dictSize;

        /* withPrefix64k : offset <= 65535 can't reach beyond the 64 KB prefix */
        const int checkOffset = (dict != withPrefix64k) && (dictSize < (int)(64 KB));


        /* Set up the "end" pointers for the shortcut. */
        const BYTE* const shortiend = iend - 14 /*maxLL*/ - 2 /*offset*/;
        const BYTE* const shortoend = oend - 14 /*maxLL*/ - 18 /*maxML*/;

        /* shared by both loops : the fast loop can hand over a decoded match (safe_match_copy) */
        const BYTE* match;
        size_t offset;
        size_t ml;   /* match length */

        /* incompleteInput : start of the current sequence, where decoding stops if it can't be completed */
        const BYTE* seqIp = NULL;
        BYTE* seqOp = NULL;

        DEBUGLOG(5, "LZ4_decompress_generic (srcSize:%i, dstSize:%i)", srcSize, outputSize);

        /* Special cases */
        assert(lowPrefix <= op);
        if (unlikely(outputSize==0)) {
            /* Empty output buffer */
            if (partialDecoding) return 0;
            if ((srcSize==1) && (*ip==0)) return 0;   /* empty block */
            return (srcSize > 1) ? -2 : -1;   /* -2 : output buffer too small */
        }
        if (unlikely(srcSize<=0)) { return -1; }

    /* LZ4_FAST_DEC_LOOP:
     * designed for modern OoO performance cpus,
     * where copying reliably 32-bytes is preferable to an unpredictable branch.
     * note : fast loop may show a regression for some client arm chips. */
#if LZ4_FAST_DEC_LOOP
        if ((oend - op) < FASTLOOP_SAFE_DISTANCE) {
            DEBUGLOG(6, "move to safe decode loop");
            goto safe_decode;
        }
        /* Ring buffer restarting at its beginning : the oldest referenced history (max distance behind op)
         * lies in the external dictionary, ahead of op, always at the same distance.
         * Rings sized with the former LZ4_DECODER_RING_BUFFER_SIZE() (margin 14, up to v1.10.0)
         * still work, but leave as little as 16 bytes :
         * the fast loop's wide copies could overwrite it, the safe loop's can't. */
        if ( (dict==usingExtDict)
          && ((uptrval)dictEnd - (uptrval)lowPrefix - LZ4_DISTANCE_ABSOLUTE_MAX < FASTLOOP_SAFE_DISTANCE) ) {
            DEBUGLOG(6, "history just ahead : move to safe decode loop");
            goto safe_decode;
        }

        /* Fast loop : decode sequences as long as output < oend-FASTLOOP_SAFE_DISTANCE,
         * and input is not too close to iend.
         * It hands over to the safe loop either at a sequence start (break),
         * or once a match is fully decoded (goto safe_match_copy). */
        DEBUGLOG(6, "using fast decode loop");
        while (1) {
            unsigned token;
            size_t ll;   /* literal length */
            BYTE* cpy;
            /* Main fastloop assertion: We can always wildcopy FASTLOOP_SAFE_DISTANCE */
            assert(oend - op >= FASTLOOP_SAFE_DISTANCE);
            assert(ip < iend);
            seqIp = ip; seqOp = op;
            token = *ip++;
            ll = token >> ML_BITS;
            DEBUGLOG(7, "blockPos%6u: litLength token = %u", (unsigned)(op-(BYTE*)dst), (unsigned)ll);

            /* too close to input end : the safe loop restarts this sequence */
            if (ip > iend-(16 + 1/*max lit + offset + nextToken*/)) { ip--; break; }

            /* decode literal length */
            if (ll == RUN_MASK) {
                /* literal length >= RUN_MASK means >= RUN_MASK literal bytes follow the extension bytes,
                 * so extension bytes cannot reach the last RUN_MASK bytes of input */
                const BYTE* const tokenPos = ip-1;
                size_t const addl = read_variable_length(&ip, iend - RUN_MASK);
                if (addl == rvl_error) {
                    DEBUGLOG(6, "error reading long literal length");
                    goto _output_error;
                }
                ll += addl;
                if (unlikely((uptrval)(op)+ll<(uptrval)(op))) { goto _output_error; } /* overflow detection */
                if (unlikely((uptrval)(ip)+ll<(uptrval)(ip))) { goto _output_error; } /* overflow detection */

                /* copy literals, unless too close to the end for a wild copy :
                 * nothing is written yet, so the safe loop can restart this sequence */
                cpy = op+ll;
                LZ4_STATIC_ASSERT(MFLIMIT >= WILDCOPYLENGTH);
              #if LZ4_WILDCOPY64
                if ((cpy>oend-64) || (ip+ll>iend-64)) { ip = tokenPos; break; }
                LZ4_wildCopy64(op, ip, cpy);
              #else
                if ((cpy>oend-32) || (ip+ll>iend-32)) { ip = tokenPos; break; }
                LZ4_wildCopy32(op, ip, cpy);
              #endif
                ip += ll; op = cpy;
            } else {
                DEBUGLOG(7, "copy %u bytes in a 16-bytes stripe", (unsigned)ll);
                /* Literals can only be <= 14, but hope compilers optimize better when copy by a register size */
                LZ4_fastCopy16(op, ip);
                ip += ll; op += ll;
            }

            /* get offset */
            offset = LZ4_fastReadLE16(ip); ip+=2;
            DEBUGLOG(6, "blockPos%6u: offset = %u", (unsigned)(op-(BYTE*)dst), (unsigned)offset);
            match = op - offset;
            assert(match <= op);  /* overflow check */

            /* get matchlength */
            ml = token & ML_MASK;
            DEBUGLOG(7, "  match length token = %u (len==%u)", (unsigned)ml, (unsigned)ml+MINMATCH);

            if (ml == ML_MASK) {
                /* after match length extension bytes, at least 1 token + LASTLITERALS literals must remain */
                size_t const addl = read_variable_length(&ip, iend - (1 + LASTLITERALS));
                if (addl == rvl_error) {
                    DEBUGLOG(5, "error reading long match length");
                    goto _output_error;
                }
                ml += addl;
                DEBUGLOG(7, "  long match length == %u", (unsigned)ml + MINMATCH);
                if (unlikely((uptrval)(op)+ml<(uptrval)op)) { goto _output_error; } /* overflow detection */
            }
            ml += MINMATCH;

            if (op + ml >= oend - FASTLOOP_SAFE_DISTANCE) {
                DEBUGLOG(7, "moving to safe_match_copy (ml==%u)", (unsigned)ml);
                goto safe_match_copy;
            }

            /* match length from token only (<= 18), no overlap, within prefix : copy 18 bytes */
            if ( ((token & ML_MASK) != ML_MASK) && (offset >= 8)
              && ((dict == withPrefix64k) || (match >= lowPrefix)) ) {
                assert(match >= lowPrefix);
                assert(match <= op);
                assert(op + 18 <= oend);
                LZ4_fastCopy18(op, match);
                op += ml;
                continue;
            }

            /* match ends far from oend : no end-of-block condition to check */
            assert(op + ml < oend - FASTLOOP_SAFE_DISTANCE);

            if ( checkOffset && (unlikely(match + dictSize < lowPrefix)) ) {
                DEBUGLOG(5, "Error : pos=%zi, offset=%zi => outside buffers", op-lowPrefix, op-match);
                goto _offset_error;
            }
            /* match starting within external dictionary */
            if ((dict==usingExtDict) && (match < lowPrefix)) {
                size_t const inDict = (size_t)(lowPrefix - match);
                assert(dictEnd != NULL);
                if (ml + 16 <= inDict) {
                    /* match within dictionary, followed by 16 more bytes of dictionary : wild copy */
                    LZ4_wildCopy16(op, dictEnd - inDict, op + ml);
                    op += ml;
                    continue;
                }
                op = LZ4_copyExtDictMatch(op, match, ml, lowPrefix, dictEnd);
                continue;
            }

            /* copy match within block */
            cpy = op + ml;

            assert((op <= oend) && (oend-op >= 64));
            if (unlikely(offset<16)) {
                LZ4_memcpy_using_offset(op, match, cpy, offset);
    #if LZ4_WILDCOPY64
            } else if (offset >= 64) {
                LZ4_wildCopy64(op, match, cpy);
    #endif
            } else {
                LZ4_wildCopy32(op, match, cpy);
            }

            op = cpy;   /* wildcopy correction */
        }
    safe_decode:
#endif

        /* Safe loop : decode remaining sequences, without writing beyond oend nor reading beyond iend.
         * With LZ4_FAST_DEC_LOOP, that's the end of the block (or a block too small for the fast loop),
         * entered at a sequence start, or at safe_match_copy with a decoded match.
         * Without it, that's the whole block. */
        DEBUGLOG(6, "using safe decode loop");
        while (1) {
            unsigned token;
            size_t ll;   /* literal length */
            BYTE* cpy;
            assert(ip < iend);
            seqIp = ip; seqOp = op;
            token = *ip++;
            ll = token >> ML_BITS;
            DEBUGLOG(7, "blockPos%6u: litLength token = %u", (unsigned)(op-(BYTE*)dst), (unsigned)ll);

            /* A two-stage shortcut for the most common case:
             * 1) If the literal length is 0..14, and there is enough space,
             * enter the shortcut and copy 16 bytes on behalf of the literals.
             * 2) Further if the match length is 4..18, copy 18 bytes in a similar
             * manner; but we ensure that there's enough space in the output for
             * those 18 bytes earlier, upon entering the shortcut (in other words,
             * there is a combined check for both stages).
             */
            if ( (ll != RUN_MASK)
                /* strictly "less than" on input, to re-enter the loop with at least one byte */
              && likely((ip < shortiend) & (op <= shortoend)) ) {
                /* Copy the literals */
                LZ4_memcpy(op, ip, 16);
                op += ll; ip += ll;

                /* The second stage: prepare for match copying, decode full info.
                 * If it doesn't work out, the info won't be wasted. */
                offset = LZ4_readLE16(ip); ip += 2;
                match = op - offset;
                assert(match <= op); /* check overflow */
                ml = token & ML_MASK;
                DEBUGLOG(7, "blockPos%6u: matchLength token = %u (len=%u)", (unsigned)(op-(BYTE*)dst), (unsigned)ml, (unsigned)ml + 4);

                /* Do not deal with overlapping matches. */
                if ( (ml != ML_MASK)
                  && (offset >= 8)
                  && (dict==withPrefix64k || match >= lowPrefix) ) {
                    /* Copy the match. */
                    LZ4_memcpy(op + 0, match + 0, 8);
                    LZ4_memcpy(op + 8, match + 8, 8);
                    LZ4_memcpy(op +16, match +16, 2);
                    op += ml + MINMATCH;
                    /* Both stages worked, load the next token. */
                    continue;
                }
                /* The second stage didn't work out, but the info is ready for the match copy below. */
            } else {
                /* decode literal length */
                if (ll == RUN_MASK) {
                    /* literal length >= RUN_MASK means >= RUN_MASK literal bytes follow the extension bytes,
                     * so extension bytes cannot reach the last RUN_MASK bytes of input */
                    size_t const addl = read_variable_length(&ip, iend-RUN_MASK);
                    if (addl == rvl_error) { goto _output_error; }
                    ll += addl;
                    if (unlikely((uptrval)(op)+ll<(uptrval)(op))) { goto _output_error; } /* overflow detection */
                    if (unlikely((uptrval)(ip)+ll<(uptrval)(ip))) { goto _output_error; } /* overflow detection */
                }

                /* copy literals */
                cpy = op+ll;
                LZ4_STATIC_ASSERT(MFLIMIT >= WILDCOPYLENGTH);
                if ((cpy>oend-MFLIMIT) || (ip+ll>iend-(2+1+LASTLITERALS))) {
                    /* close to input or output end : these must be the last literals of the block,
                     * except with partialDecoding, which can also stop within them,
                     * and incompleteInput, which stops before them : the block goes on */
                    if (incompleteInput) goto _incomplete;
                    if (partialDecoding) {
                        /* stop at input end, or when output is full */
                        ll = MIN(ll, (size_t)(iend-ip));
                        ll = MIN(ll, (size_t)(oend-op));
                    } else if ((ip+ll != iend) || (cpy > oend)) {
                        /* last literals must end exactly at input end, within output */
                        DEBUGLOG(5, "invalid last literals : they end at iend%+zi and oend%+zi", (ip+ll)-iend, cpy-oend);
                        if ((ip+ll <= iend) && (cpy > oend-MFLIMIT)) goto _dst_too_small;   /* output full, input left */
                        goto _output_error;
                    }
                    LZ4_memmove(op, ip, ll);  /* supports overlapping memory regions, for in-place decompression scenarios */
                    ip += ll;
                    op += ll;
                    /* end of block, unless partialDecoding can go on : output not full, input left for a match */
                    if (!partialDecoding || (op == oend) || (ip >= (iend-2))) {
                        break;
                    }
                } else {
                    LZ4_wildCopy8(op, ip, cpy);   /* can overwrite up to 8 bytes beyond cpy */
                    ip += ll; op = cpy;
                }

                /* get offset */
                offset = LZ4_readLE16(ip); ip+=2;
                match = op - offset;

                /* get matchlength */
                ml = token & ML_MASK;
                DEBUGLOG(7, "blockPos%6u: matchLength token = %u", (unsigned)(op-(BYTE*)dst), (unsigned)ml);
            }

            /* literals copied, offset decoded, ml == match length token */
            if (ml == ML_MASK) {
                /* after match length extension bytes, at least 1 token + LASTLITERALS literals must remain */
                size_t const addl = read_variable_length(&ip, iend - (1 + LASTLITERALS));
                if (addl == rvl_error) { goto _output_error; }
                ml += addl;
                if (unlikely((uptrval)(op)+ml<(uptrval)op)) goto _output_error;   /* overflow detection */
            }
            ml += MINMATCH;

#if LZ4_FAST_DEC_LOOP
        safe_match_copy:
#endif
            /* literals copied, match fully decoded (ml includes MINMATCH), offset not validated yet */
            if ((checkOffset) && (unlikely(match + dictSize < lowPrefix))) goto _offset_error;   /* Error : offset outside buffers */

            /* end of block : the last LASTLITERALS bytes must be literals */
            assert(op <= oend);
            if (unlikely(op+ml > oend-LASTLITERALS)) {
                if (partialDecoding) ml = MIN(ml, (size_t)(oend-op));   /* partialDecoding : may end anywhere */
                else goto _dst_too_small;
            }

            /* match starting within external dictionary */
            if ((dict==usingExtDict) && (match < lowPrefix)) {
                assert(dictEnd != NULL);
                op = LZ4_copyExtDictMatch(op, match, ml, lowPrefix, dictEnd);
                continue;
            }
            assert(match >= lowPrefix);

            /* close to block end : no wild copy */
            if (unlikely(op+ml > oend-MATCH_SAFEGUARD_DISTANCE)) {
                op = LZ4_copyMatchExact(op, match, ml);
                if (op == oend) { break; }   /* partialDecoding : output buffer full */
                continue;
            }

            /* copy match within block */
            cpy = op + ml;
            LZ4_memcpy_using_offset_base(op, match, cpy, offset);
            op = cpy;   /* wildcopy correction */
        }

        /* end of decoding */
        assert(!incompleteInput);   /* never ends the block : stops before its last literals */
        DEBUGLOG(5, "decoded %i bytes", (int) (((char*)op)-dst));
        return (int) (((char*)op)-dst);     /* Nb of output bytes decoded */

    _incomplete:   /* incompleteInput : stop before the current sequence, which can't be decoded yet */
        DEBUGLOG(5, "incomplete input : decoded %i bytes", (int)(seqOp - (BYTE*)dst));
        if (srcEndPtr != NULL) *srcEndPtr = (const char*)seqIp;
        return (int)(seqOp - (BYTE*)dst);

        /* errors : see LZ4_decompress_safe() in lz4.h
         * incompleteInput : input ends within the current sequence, or output is full,
         * or the sequence is invalid (reported once the block is complete) : stop before it */
    _output_error:
        if (incompleteInput) goto _incomplete;
        return -1;
    _dst_too_small:
        if (incompleteInput) goto _incomplete;
        return -2;
    _offset_error:
        if (incompleteInput) goto _incomplete;
        return -3;
    }
}



/*===== Instantiate the API decoding functions. =====*/

LZ4_FORCE_O2
int LZ4_decompress_safe(const char* __counted_by(compressedSize) source, char* __counted_by(maxDecompressedSize) dest, int compressedSize, int maxDecompressedSize)
{
    return LZ4_decompress_generic(source, dest, compressedSize, maxDecompressedSize,
                                  decode_full_block, noDict,
                                  (BYTE*)dest, LZ4_noHistory, 0,
                                  complete_input, NULL);
}

LZ4_FORCE_O2
int LZ4_decompress_safe_partial(const char* __counted_by(compressedSize) src, char* __counted_by(dstCapacity) dst, int compressedSize, int targetOutputSize, int dstCapacity)
{
    int const outputSize = MIN(targetOutputSize, dstCapacity);
    return LZ4_decompress_generic(src, dst, compressedSize, outputSize,
                                  partial_decode,
                                  noDict, (BYTE*)dst, LZ4_noHistory, 0,
                                  complete_input, NULL);
}

/*===== Instantiate a few more decoding cases, used more than once. =====*/

/* Safe C port: `dest` carries bounds whose lower end covers the prefixSize bytes of
 * history decoded just before it (see LZ4_decompress_safe_continue()) */
LZ4_FORCE_O2
static int LZ4_decompress_safe_withPrefix(const char* __counted_by(compressedSize) source, char* __bidi_indexable dest, int compressedSize, int maxOutputSize,
                                          size_t prefixSize)
{
    if (prefixSize >= 64 KB - 1)
        return LZ4_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                      decode_full_block, withPrefix64k,
                                      (BYTE*)dest - 64 KB, LZ4_noHistory, 0,
                                      complete_input, NULL);
    return LZ4_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                  decode_full_block, noDict,
                                  (BYTE*)dest-prefixSize, LZ4_noHistory, 0,
                                  complete_input, NULL);
}

LZ4_FORCE_O2
static int LZ4_decompress_safe_extDict(const char* __counted_by(compressedSize) source, char* __counted_by(maxOutputSize) dest,
                                       int compressedSize, int maxOutputSize,
                                       const BYTE* __bidi_indexable dictStart, size_t dictSize)
{
    return LZ4_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                  decode_full_block, usingExtDict,
                                  (BYTE*)dest, dictStart, dictSize,
                                  complete_input, NULL);
}

LZ4_FORCE_O2
static int LZ4_decompress_safe_partial_extDict(const char* __counted_by(compressedSize) source, char* __counted_by(dstCapacity) dest,
                                       int compressedSize, int targetOutputSize, int dstCapacity,
                                       const BYTE* __bidi_indexable dictStart, size_t dictSize)
{
    int const outputSize = MIN(targetOutputSize, dstCapacity);
    return LZ4_decompress_generic(source, dest, compressedSize, outputSize,
                                  partial_decode, usingExtDict,
                                  (BYTE*)dest, dictStart, dictSize,
                                  complete_input, NULL);
}

LZ4_FORCE_O2
int LZ4_decompress_safe_forceExtDict(const char* __counted_by(compressedSize) source, char* __counted_by(maxOutputSize) dest,
                                     int compressedSize, int maxOutputSize,
                                     const char* _Nullable __counted_by(dictSize) dictStart, size_t dictSize)
{
    DEBUGLOG(5, "LZ4_decompress_safe_forceExtDict");
    if (dictStart == NULL)
        return LZ4_decompress_safe_extDict(source, dest, compressedSize, maxOutputSize, LZ4_noHistory, 0);
    return LZ4_decompress_safe_extDict(source, dest, compressedSize, maxOutputSize, (const BYTE*)dictStart, dictSize);
}

LZ4_FORCE_O2
int LZ4_decompress_safe_partial_forceExtDict(const char* __counted_by(compressedSize) source, char* __counted_by(dstCapacity) dest,
                                     int compressedSize, int targetOutputSize, int dstCapacity,
                                     const char* _Nullable __counted_by(dictSize) dictStart, size_t dictSize)
{
    if (dictStart == NULL)
        return LZ4_decompress_safe_partial_extDict(source, dest, compressedSize, targetOutputSize, dstCapacity, LZ4_noHistory, 0);
    return LZ4_decompress_safe_partial_extDict(source, dest, compressedSize, targetOutputSize, dstCapacity, (const BYTE*)dictStart, dictSize);
}

/* The "double dictionary" mode, for use with e.g. ring buffers: the first part
 * of the dictionary is passed as prefix, and the second via dictStart + dictSize.
 * These routines are used only once, in LZ4_decompress_*_continue().
 * Safe C port: as for LZ4_decompress_safe_withPrefix(), `dest` covers the prefix. */
LZ4_FORCE_INLINE
int LZ4_decompress_safe_doubleDict(const char* __counted_by(compressedSize) source, char* __bidi_indexable dest, int compressedSize, int maxOutputSize,
                                   size_t prefixSize, const BYTE* __bidi_indexable dictStart, size_t dictSize)
{
    return LZ4_decompress_generic(source, dest, compressedSize, maxOutputSize,
                                  decode_full_block, usingExtDict,
                                  (BYTE*)dest-prefixSize, dictStart, dictSize,
                                  complete_input, NULL);
}

/* incomplete block : a single instantiation, history being a prefix or an external dictionary
 * Safe C port: a history that ends at dst (upstream's prefix) is decoded as an external
 * dictionary : the same bytes, without a pointer spanning two buffers. */
LZ4_FORCE_O2
int LZ4_decompress_incomplete(const char* __counted_by(*srcSizePtr) src, char* __counted_by(dstCapacity) dst, int* srcSizePtr, int dstCapacity,
                              const char* _Nullable __counted_by(dictSize) dictStart, int dictSize)
{
    const char* srcEnd = src;
    const BYTE* dict = LZ4_noHistory;
    size_t dSize = 0;
    int const srcSize = *srcSizePtr;
    int decodedSize;
    if ((srcSize < 0) || (dictSize < 0)) return -1;
    if ((srcSize == 0) || (dstCapacity == 0)) { *srcSizePtr = 0; return 0; }   /* nothing to decode yet */
    if ((dictStart != NULL) && (dictSize > 0)) { dict = (const BYTE*)dictStart; dSize = (size_t)dictSize; }
    decodedSize = LZ4_decompress_generic(src, dst, srcSize, dstCapacity,
                                  decode_full_block, usingExtDict,
                                  (BYTE*)dst, dict, dSize,
                                  incomplete_input, &srcEnd);
    if (decodedSize >= 0) *srcSizePtr = (int)(srcEnd - src);
    return decodedSize;
}

/*===== streaming decompression functions =====*/

_Returns_owned LZ4_streamDecode_t* _Nullable LZ4_createStreamDecode(void)
{
    LZ4_STATIC_ASSERT(sizeof(LZ4_streamDecode_t) >= sizeof(LZ4_streamDecode_t_internal));
    /* Safe C port: LZ4_streamDecode_t lost its char[LZ4_STREAMDECODE_MINSIZE] member */
    LZ4_STATIC_ASSERT((sizeof(void*) != 8) || (sizeof(LZ4_streamDecode_t) == LZ4_STREAMDECODE_MINSIZE));
    return (LZ4_streamDecode_t*) ALLOC_AND_ZERO(sizeof(LZ4_streamDecode_t));
}

int LZ4_freeStreamDecode (LZ4_streamDecode_t* _Owned _Nullable LZ4_stream)
{
    if (LZ4_stream == NULL) { return 0; }  /* support free on NULL */
    FREEMEM(LZ4_stream);
    return 0;
}

/*! LZ4_setStreamDecode() :
 *  Use this function to instruct where to find the dictionary.
 *  This function is not necessary if previous data is still available where it was decoded.
 *  Loading a size of 0 is allowed (same effect as no dictionary).
 * @return : 1 if OK, 0 if error
 */
int LZ4_setStreamDecode (LZ4_streamDecode_t* LZ4_streamDecode, const char* _Nullable __counted_by(dictSize) dictionary, int dictSize)
{
    LZ4_streamDecode_t_internal* lz4sd = &LZ4_streamDecode->internal_donotuse;
    if (dictSize < 0) return 0;
    if (dictSize && (dictionary != NULL)) {
        lz4sd->prefixSize = (size_t)dictSize;
        lz4sd->prefixEnd = (const BYTE*) dictionary + dictSize;
    } else {
        lz4sd->prefixSize = 0;   /* Safe C port: also for a NULL dictionary */
        lz4sd->prefixEnd = (const BYTE*) dictionary;
    }
    lz4sd->externalDict = NULL;
    lz4sd->extDictSize  = 0;
    return 1;
}

/*! LZ4_decoderRingBufferSize() :
 *  when setting a ring buffer for streaming decompression (optional scenario),
 *  provides the minimum size of this ring buffer
 *  to be compatible with any source respecting maxBlockSize condition.
 *  Note : in a ring buffer scenario,
 *  blocks are presumed decompressed next to each other.
 *  When not enough space remains for next block (remainingSize < maxBlockSize),
 *  decoding resumes from beginning of ring buffer.
 * @return : minimum ring buffer size,
 *           or 0 if there is an error (invalid maxBlockSize).
 */
int LZ4_decoderRingBufferSize(int maxBlockSize)
{
    /* the margin must cover the widest write beyond a copy of the safe loop (16 bytes), minus 2 :
     * LZ4_decompress_generic() avoids the fast loop when history is closer than FASTLOOP_SAFE_DISTANCE,
     * which the current margin (64) never is. The former margin (14) remains valid. */
    LZ4_STATIC_ASSERT(LZ4_DECODER_RING_BUFFER_SIZE(0) - 65536 >= 16 - 2);
    if (maxBlockSize < 0) return 0;
    if (maxBlockSize > LZ4_MAX_INPUT_SIZE) return 0;
    if (maxBlockSize < 16) maxBlockSize = 16;
    return LZ4_DECODER_RING_BUFFER_SIZE(maxBlockSize);
}

/*
*_continue() :
    These decoding functions allow decompression of multiple blocks in "streaming" mode.
    Previously decoded blocks must still be available at the memory position where they were decoded.
    If it's not possible, save the relevant part of decoded data into a safe buffer,
    and indicate where it stands using LZ4_setStreamDecode()
*/
LZ4_FORCE_O2
int LZ4_decompress_safe_continue (LZ4_streamDecode_t* LZ4_streamDecode, const char* __counted_by(compressedSize) source, char* __counted_by(maxOutputSize) dest, int compressedSize, int maxOutputSize)
{
    LZ4_streamDecode_t_internal* lz4sd = &LZ4_streamDecode->internal_donotuse;
    int result;
    uptrval prefixEnd = 0;
    __unsafe { prefixEnd = (uptrval)lz4sd->prefixEnd; }   /* reads the address only */   // GAP 4

    if (lz4sd->prefixSize == 0) {
        /* The first call, no dictionary yet. */
        assert(lz4sd->extDictSize == 0);
        result = LZ4_decompress_safe(source, dest, compressedSize, maxOutputSize);
        if (result <= 0) return result;
        lz4sd->prefixSize = (size_t)result;
        lz4sd->prefixEnd = (BYTE*)dest + result;
    } else if (prefixEnd == (uptrval)dest) {
        /* They're rolling the current segment. */
        char* wide = dest;
        /* the previous blocks were decoded just before dest (prefixEnd == dest, checked above),
         * prefixSize bytes the streaming API requires to stay alive : one buffer */
        __unsafe { wide = __unsafe_forge_bidi_indexable(char*, (char*)lz4sd->prefixEnd - lz4sd->prefixSize, lz4sd->prefixSize + (size_t)maxOutputSize) + lz4sd->prefixSize; }   // GAP 1, GAP 3
        if ((lz4sd->prefixSize >= 64 KB - 1) || (lz4sd->extDictSize == 0)) {
            result = LZ4_decompress_safe_withPrefix(source, wide, compressedSize, maxOutputSize,
                                                    lz4sd->prefixSize);
        } else {
            const BYTE* ext = LZ4_noHistory;
            /* externalDict is earlier output, extDictSize bytes the streaming API requires to stay alive */
            __unsafe { ext = __unsafe_forge_bidi_indexable(const BYTE*, lz4sd->externalDict, lz4sd->extDictSize); }   // GAP 1
            result = LZ4_decompress_safe_doubleDict(source, wide, compressedSize, maxOutputSize,
                                                    lz4sd->prefixSize, ext, lz4sd->extDictSize);
        }
        if (result <= 0) return result;
        lz4sd->prefixSize += (size_t)result;
        lz4sd->prefixEnd  = (BYTE*)dest + result;   /* Safe C port: was prefixEnd += result (prefixEnd == dest) */
    } else {
        /* The buffer wraps around, or they're switching to another buffer. */
        const BYTE* ext = LZ4_noHistory;
        lz4sd->extDictSize = lz4sd->prefixSize;
        /* the previous prefix, prefixSize bytes ending at prefixEnd, which the streaming API requires to stay alive */
        __unsafe {   // GAP 1
            lz4sd->externalDict = lz4sd->prefixEnd - lz4sd->extDictSize;
            ext = __unsafe_forge_bidi_indexable(const BYTE*, lz4sd->externalDict, lz4sd->extDictSize);
        }
        result = LZ4_decompress_safe_extDict(source, dest, compressedSize, maxOutputSize,
                                             ext, lz4sd->extDictSize);
        if (result <= 0) return result;
        lz4sd->prefixSize = (size_t)result;
        lz4sd->prefixEnd  = (BYTE*)dest + result;
    }

    return result;
}


/*
Advanced decoding functions :
*_usingDict() :
    These decoding functions work the same as "_continue" ones,
    the dictionary must be explicitly provided within parameters
    Safe C port: a dictionary that ends at dest (upstream's faster prefix mode) is decoded
    as an external dictionary : the same bytes, without a pointer spanning two buffers.
*/

int LZ4_decompress_safe_usingDict(const char* __counted_by(compressedSize) source, char* __counted_by(maxOutputSize) dest, int compressedSize, int maxOutputSize, const char* _Nullable __counted_by(dictSize) dictStart, int dictSize)
{
    if (dictSize < 0) return -1;
    if ((dictSize==0) || (dictStart == NULL))
        return LZ4_decompress_safe(source, dest, compressedSize, maxOutputSize);
    return LZ4_decompress_safe_extDict(source, dest, compressedSize, maxOutputSize, (const BYTE*)dictStart, (size_t)dictSize);
}

int LZ4_decompress_safe_partial_usingDict(const char* __counted_by(compressedSize) source, char* __counted_by(dstCapacity) dest, int compressedSize, int targetOutputSize, int dstCapacity, const char* _Nullable __counted_by(dictSize) dictStart, int dictSize)
{
    if (dictSize < 0) return -1;
    if ((dictSize==0) || (dictStart == NULL))
        return LZ4_decompress_safe_partial(source, dest, compressedSize, targetOutputSize, dstCapacity);
    return LZ4_decompress_safe_partial_extDict(source, dest, compressedSize, targetOutputSize, dstCapacity, (const BYTE*)dictStart, (size_t)dictSize);
}

#endif   /* LZ4_COMMONDEFS_ONLY */
