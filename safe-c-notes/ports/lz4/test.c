/* Round-trip and malformed-input tests for the Safe C lz4 port. Builds as Safe C
 * (-fsafe-c) and as plain C. Run from this directory (it reads lz4.c and lz4.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lz4.h"

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* compress, decompress, compare; then corrupt and truncate the block */
static void roundtrip(const char *name, const char *_Nullable __counted_by(n) src, int n)
{
    int const bound = LZ4_compressBound(n);
    char *_Nullable comp = malloc((size_t)bound + 1);
    char *_Nullable back = malloc((size_t)n + 1);
    int csize, dsize, i;
    if (comp == NULL || back == NULL) { CHECK(0, "%s: out of memory", name); free(comp); free(back); return; }

    csize = LZ4_compress_default(src, comp, n, bound);
    CHECK(csize > 0, "%s: compress returned %d", name, csize);
    dsize = LZ4_decompress_safe(comp, back, csize, n);
    CHECK(dsize == n, "%s: decompressed %d of %d bytes", name, dsize, n);
    CHECK(n == 0 || src == NULL || memcmp(src, back, (size_t)n) == 0, "%s: round trip differs", name);

    /* too small an output buffer is rejected */
    if (n > 16) {
        dsize = LZ4_decompress_safe(comp, back, csize, n - 1);
        CHECK(dsize < 0, "%s: dst one byte short accepted (%d)", name, dsize);
    }
    /* truncated input is rejected */
    for (i = 1; i <= 16 && i < csize; i++) {
        dsize = LZ4_decompress_safe(comp, back, csize - i, n);
        CHECK(dsize < 0, "%s: input truncated by %d accepted (%d)", name, i, dsize);
    }
    /* corrupted input never crashes and never claims more than dst */
    for (i = 0; i < 2000 && csize > 0; i++) {
        int const pos = (int)(((unsigned)i * 2654435761u) % (unsigned)csize);
        char const saved = comp[pos];
        comp[pos] = (char)(comp[pos] ^ (char)(1 + i % 255));
        dsize = LZ4_decompress_safe(comp, back, csize, n);
        CHECK(dsize <= n, "%s: corrupted input decoded to %d > %d", name, dsize, n);
        comp[pos] = saved;
    }
    /* partial decoding of a prefix */
    if (n > 100) {
        dsize = LZ4_decompress_safe_partial(comp, back, csize, 100, n);
        CHECK(dsize >= 100 && memcmp(src == NULL ? back : src, back, 100) == 0, "%s: partial decode %d", name, dsize);
    }
    printf("%-10s %8d -> %8d bytes\n", name, n, csize);
    free(comp);
    free(back);
}

/* streaming: 64 KB blocks of one buffer (prefix mode), then with an external dictionary */
static void streaming(const char *name, const char *__counted_by(n) src, int n)
{
    enum { BLOCK = 65536 };
    int const bound = LZ4_compressBound(BLOCK);
    char *_Nullable comp = malloc((size_t)(n / BLOCK + 1) * (size_t)(bound + 4));
    char *_Nullable back = malloc((size_t)n + 1);
    LZ4_stream_t *_Nullable cs = LZ4_createStream();
    LZ4_streamDecode_t *_Nullable ds = LZ4_createStreamDecode();
    int pos = 0, cpos = 0, sizes[64], nb = 0, i, opos = 0;
    if (comp == NULL || back == NULL || cs == NULL || ds == NULL || n / BLOCK + 1 > 64) {
        CHECK(0, "%s: setup", name);
    } else {
        while (pos < n && nb < 64) {
            int const len = (n - pos < BLOCK) ? n - pos : BLOCK;
            int const c = LZ4_compress_fast_continue(cs, src + pos, comp + cpos, len, bound, 1);
            CHECK(c > 0, "%s: block %d compress %d", name, nb, c);
            sizes[nb++] = c; cpos += c; pos += len;
        }
        cpos = 0;
        for (i = 0; i < nb; i++) {
            int const len = (n - opos < BLOCK) ? n - opos : BLOCK;
            int const d = LZ4_decompress_safe_continue(ds, comp + cpos, back + opos, sizes[i], len);
            CHECK(d == len, "%s: block %d decompress %d of %d", name, i, d, len);
            if (d != len) break;
            cpos += sizes[i]; opos += d;
        }
        CHECK(opos == n && memcmp(src, back, (size_t)n) == 0, "%s: streaming round trip differs", name);

        /* dictionary: the first 64 KB compress the rest; decode with usingDict (prefix and separate) */
        if (n > 2 * BLOCK) {
            int const rest = (n - BLOCK < BLOCK) ? n - BLOCK : BLOCK;
            int c, d;
            LZ4_loadDict(cs, src, BLOCK);
            c = LZ4_compress_fast_continue(cs, src + BLOCK, comp, rest, bound, 1);
            CHECK(c > 0, "%s: dict compress", name);
            d = LZ4_decompress_safe_usingDict(comp, back, c, rest, src, BLOCK);
            CHECK(d == rest && memcmp(src + BLOCK, back, (size_t)rest) == 0, "%s: usingDict (separate) %d", name, d);
            memcpy(back, src, BLOCK);
            d = LZ4_decompress_safe_usingDict(comp, back + BLOCK, c, rest, back, BLOCK);
            CHECK(d == rest && memcmp(src, back, (size_t)(BLOCK + rest)) == 0, "%s: usingDict (prefix) %d", name, d);
        }
        printf("%-10s %8d bytes in %d streamed blocks: ok\n", name, n, nb);
    }
    LZ4_freeStream(cs);
    LZ4_freeStreamDecode(ds);
    free(comp);
    free(back);
}

static void destSize(const char *__counted_by(n) src, int n)
{
    char dst[1000];
    int srcSize = n;
    char *_Nullable back = malloc((size_t)n + 1);
    int c, d;
    if (back == NULL) return;
    c = LZ4_compress_destSize(src, dst, &srcSize, (int)sizeof(dst));
    CHECK(c > 0 && c <= (int)sizeof(dst) && srcSize > 0 && srcSize <= n, "destSize: %d bytes from %d", c, srcSize);
    d = LZ4_decompress_safe(dst, back, c, n);
    CHECK(d == srcSize && memcmp(src, back, (size_t)d) == 0, "destSize: round trip %d", d);
    free(back);
}

static int read_file(const char *path, char *__counted_by(cap) buf, int cap)
{
    FILE *_Nullable f = fopen(path, "rb");
    size_t got;
    if (f == NULL) return -1;
    got = fread(buf, 1, (size_t)cap, f);
    fclose(f);
    return (int)got;
}

int main(void)
{
    enum { MB = 1 << 20 };
    char *_Nullable buf = malloc(MB);
    unsigned x = 2463534242u;
    int i, n, m;
    if (buf == NULL) return 1;

    /* (a) the lz4 sources themselves */
    n = read_file("lz4.c", buf, MB);
    m = n > 0 ? read_file("lz4.h", buf + n, MB - n) : -1;
    CHECK(n > 0 && m > 0, "cannot read lz4.c / lz4.h (run from the port directory)");
    if (n > 0 && m > 0) { roundtrip("sources", buf, n + m); destSize(buf, n + m); }

    /* (b) 1 MB of random bytes */
    for (i = 0; i < MB; i++) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; buf[i] = (char)x; }
    roundtrip("random", buf, MB);
    streaming("random", buf, MB);

    /* (c) 1 MB of a repeated pattern */
    for (i = 0; i < MB; i++) buf[i] = "lz4 under safe c, "[i % 18];
    roundtrip("pattern", buf, MB);
    streaming("pattern", buf, MB);

    /* (d) empty input */
    roundtrip("empty", NULL, 0);
    {   char out[16];
        int const c = LZ4_compress_default(NULL, out, 0, (int)sizeof(out));
        CHECK(c == 1, "empty: compress %d", c);
        CHECK(LZ4_decompress_safe(out, buf, c, 0) == 0, "empty: decompress");
    }

    free(buf);
    printf("%s: %d failure(s)\n", failures ? "FAILED" : "all passed", failures);
    return failures != 0;
}
