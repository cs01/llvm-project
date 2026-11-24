/* Compress/decompress speed of the lz4 port: bench <file>. Best of 5 runs each.
 * Builds as Safe C and as plain C. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "lz4.h"

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

int main(int argc, char *__counted_by(argc) *argv)
{
    enum { CAP = 64 << 20, RUNS = 5 };
    char *_Nullable src = malloc(CAP);
    char *_Nullable comp = malloc((size_t)LZ4_compressBound(CAP));
    char *_Nullable back = malloc(CAP);
    FILE *_Nullable f;
    int n, c = 0, d = 0, r;
    double bestC = 1e9, bestD = 1e9;
    if (argc < 2 || src == NULL || comp == NULL || back == NULL) return 2;
    f = fopen(argv[1], "rb");
    if (f == NULL) return 2;
    n = (int)fread(src, 1, CAP, f);
    fclose(f);
    for (r = 0; r < RUNS; r++) {
        double t0 = now(), t1;
        c = LZ4_compress_default(src, comp, n, LZ4_compressBound(CAP));
        t1 = now();
        if (t1 - t0 < bestC) bestC = t1 - t0;
    }
    for (r = 0; r < RUNS; r++) {
        double t0 = now(), t1;
        d = LZ4_decompress_safe(comp, back, c, CAP);
        t1 = now();
        if (t1 - t0 < bestD) bestD = t1 - t0;
    }
    if (d != n || memcmp(src, back, (size_t)n) != 0) { printf("round trip failed\n"); return 1; }
    printf("%d bytes -> %d (%.3f)  compress %.1f MB/s  decompress %.1f MB/s\n",
           n, c, (double)c / n, n / bestC / 1e6, n / bestD / 1e6);
    free(src); free(comp); free(back);
    return 0;
}
