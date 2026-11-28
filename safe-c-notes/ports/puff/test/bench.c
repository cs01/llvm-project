/* Speed: the Safe C puff, the original puff and zlib's inflate, decoding the
   same raw deflate stream of a file. bench file [reps] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
int puff(unsigned char *, unsigned long *, const unsigned char *, unsigned long *);
int orig_puff(unsigned char *, unsigned long *, const unsigned char *, unsigned long *);
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec + t.tv_nsec / 1e9; }
int main(int argc, char **argv) {
  FILE *f = fopen(argv[1], "rb"); if (!f) return 1;
  fseek(f, 0, SEEK_END); size_t n = (size_t)ftell(f); rewind(f);
  unsigned char *in = malloc(n), *c = malloc(n + n / 8 + 1024), *out = malloc(n);
  if (fread(in, 1, n, f) != n) return 1;
  int reps = argc > 2 ? atoi(argv[2]) : 5;
  z_stream z = {0}; deflateInit2(&z, 6, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY);
  z.next_in = in; z.avail_in = (unsigned)n; z.next_out = c; z.avail_out = (unsigned)(n + n / 8 + 1024);
  deflate(&z, Z_FINISH); size_t m = z.total_out; deflateEnd(&z);
  const char *name[3] = {"original puff", "Safe C puff", "zlib inflate"};
  double best[3] = {1e9, 1e9, 1e9};
  for (int r = 0; r < reps; r++)
    for (int k = 0; k < 3; k++) {
      memset(out, 0, n);
      unsigned long dl = n, sl = m; double t = now(); int e = 0;
      if (k == 0) e = orig_puff(out, &dl, c, &sl);
      if (k == 1) e = puff(out, &dl, c, &sl);
      if (k == 2) { z_stream d = {0}; inflateInit2(&d, -15); d.next_in = c; d.avail_in = (unsigned)m; d.next_out = out; d.avail_out = (unsigned)n; e = inflate(&d, Z_FINISH) == Z_STREAM_END ? 0 : 1; dl = d.total_out; inflateEnd(&d); }
      t = now() - t;
      if (e || dl != n || memcmp(out, in, n)) { printf("%s failed\n", name[k]); return 1; }
      if (t < best[k]) best[k] = t;
    }
  printf("%zu bytes, %zu compressed, best of %d:\n", n, m, reps);
  for (int k = 0; k < 3; k++) printf("  %-14s %7.1f MB/s  %.2fx the original\n", name[k], n / best[k] / 1e6, best[0] / best[k]);
  return 0;
}
