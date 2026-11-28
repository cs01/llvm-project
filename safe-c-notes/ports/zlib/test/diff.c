/* The Safe C inflate against stock zlib, in one program: stock is built with
   Z_PREFIX (z_inflate, ...), the port without. Plain C.
     diff cases seed    streams from zlib (raw, zlib and gzip wrappers), a third
                        cut short, a third with bits flipped, read out in
                        chunks of random size (so the window is used); both
                        must agree on every return code and byte
     bench file         MB/s of each, whole buffer and 16 KB chunks */
#define Z_PREFIX
#define ZLIB_CONST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "zlib.h"
#undef inflateInit2_
#undef inflate
#undef inflateEnd
int inflateInit2_(z_streamp, int, const char *, int);
int inflate(z_streamp, int);
int inflateEnd(z_streamp);

static unsigned long long rs;
static unsigned rnd(void) { rs = rs * 6364136223846793005ULL + 1442695040888963407ULL; return (unsigned)(rs >> 33); }

/* Inflates all of in, out_chunk bytes of room at a time; returns the last
   return code and sets *n to the bytes written. */
static int run(int safe, int wbits, const unsigned char *in, size_t m, unsigned char *out, size_t cap, size_t chunk, size_t *n) {
  z_stream z; memset(&z, 0, sizeof z);
  int r = safe ? inflateInit2_(&z, wbits, ZLIB_VERSION, (int)sizeof z) : z_inflateInit2_(&z, wbits, ZLIB_VERSION, (int)sizeof z);
  if (r != Z_OK) return 100 + r;
  z.next_in = in; z.avail_in = (unsigned)m;
  do {
    size_t room = cap - z.total_out; if (room > chunk) room = chunk;
    z.next_out = out + z.total_out; z.avail_out = (unsigned)room;
    r = safe ? inflate(&z, Z_NO_FLUSH) : z_inflate(&z, Z_NO_FLUSH);
  } while (r == Z_OK && z.total_out < cap);
  *n = z.total_out;
  if (safe) inflateEnd(&z); else z_inflateEnd(&z);
  return r;
}

static size_t make(unsigned char *p, size_t cap) {
  size_t n = rnd() % cap;
  unsigned kind = rnd() % 4, alpha = 1 + rnd() % 255;
  for (size_t i = 0; i < n; i++) {
    if (kind == 0) p[i] = (unsigned char)rnd();
    else if (kind == 1) p[i] = (unsigned char)(rnd() % alpha);
    else if (kind == 2 && i > 300 && rnd() % 8) p[i] = p[i - 1 - rnd() % (i < 40000 ? i : 40000)];
    else p[i] = "the quick brown fox jumps "[rnd() % 26];
  }
  return n;
}

static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }

int main(int argc, char **argv) {
  if (argc > 2 && !strcmp(argv[1], "bench")) {
    FILE *f = fopen(argv[2], "rb"); if (!f) return 1;
    fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); rewind(f);
    unsigned char *in = malloc(n), *out = malloc(n);
    uLongf m = z_compressBound(n); unsigned char *c = malloc(m);
    if (fread(in, 1, n, f) != n || z_compress2(c, &m, in, n, 6) != Z_OK) return 1;
    size_t chunks[2] = {n, 16384};
    for (int k = 0; k < 2; k++) {
      double best[2] = {1e9, 1e9};
      for (int r = 0; r < 7; r++)
        for (int safe = 0; safe < 2; safe++) {
          size_t got; double t = now();
          int e = run(safe, 15, c, m, out, n, chunks[k], &got);
          t = now() - t;
          if (e != Z_STREAM_END || got != n || memcmp(in, out, n)) { printf("%s failed\n", safe ? "Safe C" : "stock"); return 1; }
          if (t < best[safe]) best[safe] = t;
        }
      printf("%s: stock %.0f MB/s, Safe C %.0f MB/s (%.2fx)\n", k ? "16 KB chunks" : "whole buffer",
             n / best[0] / 1e6, n / best[1] / 1e6, best[0] / best[1]);
    }
    return 0;
  }
  long cases = argc > 1 ? atol(argv[1]) : 20000;
  rs = argc > 2 ? strtoull(argv[2], 0, 10) : 1;
  enum { CAP = 1 << 17 };
  static unsigned char data[CAP], comp[CAP * 2], a[CAP], b[CAP];
  long ends = 0, other = 0;
  for (long c = 0; c < cases; c++) {
    size_t n = make(data, c % 4 ? 8192 : CAP);
    static const int wb[] = {-15, 15, 31, -9, 9};
    int w = wb[rnd() % 5];
    z_stream z; memset(&z, 0, sizeof z);
    if (z_deflateInit2_(&z, (int)(rnd() % 10), Z_DEFLATED, w, 1 + (int)(rnd() % 9), (int)(rnd() % 5), ZLIB_VERSION, (int)sizeof z) != Z_OK) return 2;
    z.next_in = data; z.avail_in = (unsigned)n; z.next_out = comp; z.avail_out = sizeof comp;
    if (z_deflate(&z, Z_FINISH) != Z_STREAM_END) return 3;
    size_t m = z.total_out; z_deflateEnd(&z);
    int mut = c % 3;
    if (mut == 1 && m) m = rnd() % m;
    if (mut == 2) for (unsigned k = 1 + rnd() % 8; k--; ) if (m) comp[rnd() % m] ^= (unsigned char)(1u << (rnd() % 8));
    size_t chunk = rnd() % 4 ? 1 + rnd() % 4096 : CAP, na, nb;
    if (w == 9 || w == -9) w = w * 15 / 9; /* inflate needs a window at least as big */
    int ra = run(0, w, comp, m, a, sizeof a, chunk, &na);
    int rb = run(1, w, comp, m, b, sizeof b, chunk, &nb);
    if (ra != rb || na != nb || memcmp(a, b, na)) {
      printf("case %ld: stock %d (%zu bytes), Safe C %d (%zu bytes)\n", c, ra, na, rb, nb);
      return 1;
    }
    if (ra == Z_STREAM_END) {
      ends++;
      if (mut == 0 && (na != n || memcmp(a, data, n))) { printf("case %ld: output differs from the input\n", c); return 1; }
    } else other++;
  }
  printf("%ld cases: %ld decoded, %ld errors or cut short, the same as stock zlib on every one\n", cases, ends, other);
  return 0;
}
