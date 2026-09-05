// A header the way a real project would ship it: contracts behind a shim so
// every other compiler sees nothing.
#ifndef CODEC_H
#define CODEC_H

#if defined(__clang__) && __has_feature(c_contracts)
#  define CODEC_PRE(...)  pre(__VA_ARGS__)
#  define CODEC_POST(...) post(__VA_ARGS__)
#else
#  define CODEC_PRE(...)
#  define CODEC_POST(...)
#endif

int codec_is_error(int r) __attribute__((pure));

// The design doc's headline example, written as it would really appear.
unsigned long codec_decompress(void *dst, unsigned long dstCap,
                               const void *src, unsigned long srcSize)
  CODEC_PRE (dst != 0)
  CODEC_PRE (src != 0)
  CODEC_PRE (dstCap > 0)
  CODEC_POST (r: r <= old(dstCap) || codec_is_error((int)r));

// Contracts live on the prototype; the definition restates nothing.
int *codec_alloc(unsigned long n)
  CODEC_PRE  (n > 0)
  CODEC_POST (r: r != 0);

#endif
