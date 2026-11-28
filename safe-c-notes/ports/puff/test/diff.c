/* Differential test: the Safe C puff against the original puff and zlib.
   Streams from zlib at every level and strategy, then the same streams cut
   short and with bits flipped. Both decoders must agree on the return value,
   the lengths and, on success, every byte; on success the bytes must be what
   was compressed. Plain C: the harness is not ported.
     diff [cases] [seed]          default 200000 1 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

int puff(unsigned char *dest, unsigned long *destlen, const unsigned char *source, unsigned long *sourcelen);
int orig_puff(unsigned char *dest, unsigned long *destlen, const unsigned char *source, unsigned long *sourcelen);

static unsigned long long rs;
static unsigned rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }

/* Data from nearly random to very repetitive, so every block type and code shows up. */
static size_t make(unsigned char *p, size_t cap) {
  size_t n = rnd() % cap;
  unsigned kind = rnd() % 4, alpha = 1 + rnd() % 255;
  for (size_t i = 0; i < n; i++) {
    if (kind == 0) p[i] = (unsigned char)rnd();
    else if (kind == 1) p[i] = (unsigned char)(rnd() % alpha);
    else if (kind == 2 && i > 300 && rnd() % 8) p[i] = p[i - 1 - rnd() % 300];
    else p[i] = "the quick brown fox jumps "[rnd() % 26];
  }
  return n;
}

static size_t deflate_raw(const unsigned char *in, size_t n, unsigned char *out, size_t cap) {
  z_stream z; memset(&z, 0, sizeof z);
  static const int strat[] = {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED};
  if (deflateInit2(&z, (int)(rnd() % 10), Z_DEFLATED, -15, 1 + (int)(rnd() % 9), strat[rnd() % 5]) != Z_OK) exit(2);
  z.next_in = (unsigned char *)in; z.avail_in = (unsigned)n;
  z.next_out = out; z.avail_out = (unsigned)cap;
  if (deflate(&z, Z_FINISH) != Z_STREAM_END) exit(3);
  size_t m = z.total_out; deflateEnd(&z); return m;
}

enum { CAP = 1 << 16 };
static unsigned char data[CAP], comp[CAP * 2], a[CAP], b[CAP];

int main(int argc, char **argv) {
  long cases = argc > 1 ? atol(argv[1]) : 200000;
  rs = argc > 2 ? strtoull(argv[2], 0, 10) : 1;
  long ok = 0, errs = 0, roundtrip = 0;
  for (long c = 0; c < cases; c++) {
    size_t n = make(data, c % 4 ? 4096 : CAP);
    size_t m = deflate_raw(data, n, comp, sizeof comp);
    int mut = c % 3; /* 0: as is, 1: cut short, 2: bits flipped */
    if (mut == 1 && m) m = rnd() % m;
    if (mut == 2) for (unsigned k = 1 + rnd() % 8; k--; ) if (m) comp[rnd() % m] ^= (unsigned char)(1u << (rnd() % 8));
    unsigned long outcap = rnd() % 16 ? sizeof a : rnd() % (n + 1); /* sometimes too small */
    int nil = rnd() % 16 == 0;
    memset(a, 0xAA, sizeof a); memset(b, 0xAA, sizeof b);
    unsigned long da = outcap, sa = m, db = outcap, sb = m;
    int ra = orig_puff(nil ? 0 : a, &da, comp, &sa);
    int rb = puff(nil ? 0 : b, &db, comp, &sb);
    int same = ra == rb && (ra > 0 || (da == db && sa == sb && (nil || !memcmp(a, b, da))));
    if (!same) {
      printf("case %ld (mut %d): original %d out %lu in %lu, Safe C %d out %lu in %lu\n", c, mut, ra, da, sa, rb, db, sb);
      return 1;
    }
    if (ra == 0) {
      ok++;
      if (mut == 0 && !nil && outcap == sizeof a) {
        if (da != n || memcmp(a, data, n)) { printf("case %ld: decoded bytes differ from the input\n", c); return 1; }
        roundtrip++;
      }
    } else errs++;
  }
  printf("%ld cases: %ld decoded (%ld checked against the input), %ld errors, all the same as the original\n", cases, ok, roundtrip, errs);
  return 0;
}
