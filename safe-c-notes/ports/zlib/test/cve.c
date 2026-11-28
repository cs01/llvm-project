/* CVE-2022-37434: inflate() copied a gzip header's extra field into
   head->extra past extra_max when the field arrived over several calls
   (zlib 1.2.12 and before). This feeds a 200-byte extra field, 1 to 4 bytes
   per call, into a 16-byte buffer. Fixed zlib keeps the first 16 bytes; with
   the fix removed, stock zlib writes past the buffer (ASan reports it) and the
   Safe C port stops at the copy. Plain C. Exit 0 when the result is right. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zlib.h"

#define XLEN 200

int main(void) {
    unsigned char gz[10 + 2 + XLEN + 5 + 8];
    size_t n = 0, at = 0;
    int i, r = Z_OK, calls = 0;
    unsigned char out[64];
    unsigned char *extra = malloc(16);  /* on the heap, so ASan sees it */
    z_stream s;
    gz_header head;

    /* gzip header with FEXTRA, the extra field, an empty stored final block,
       then CRC-32 and length (both 0 for no data) */
    static const unsigned char hdr[10] = {0x1f, 0x8b, 8, 4, 0, 0, 0, 0, 0, 3};
    memcpy(gz, hdr, 10); n = 10;
    gz[n++] = XLEN & 0xff; gz[n++] = XLEN >> 8;
    for (i = 0; i < XLEN; i++) gz[n++] = (unsigned char)(i + 1);
    static const unsigned char tail[13] = {1, 0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 0};
    memcpy(gz + n, tail, 13); n += 13;

    memset(&s, 0, sizeof s);
    memset(&head, 0, sizeof head);
    if (inflateInit2(&s, 15 + 16) != Z_OK) return 2;
    head.extra = extra;
    head.extra_max = 16;
    if (inflateGetHeader(&s, &head) != Z_OK) return 2;
    while (at < n && r == Z_OK) {
        unsigned chunk = 1 + calls % 4;
        if (chunk > n - at) chunk = (unsigned)(n - at);
        s.next_in = gz + at;
        s.avail_in = chunk;
        s.next_out = out;
        s.avail_out = sizeof out;
        r = inflate(&s, Z_NO_FLUSH);
        at += chunk - s.avail_in;
        calls++;
    }
    inflateEnd(&s);
    for (i = 0; i < 16; i++)
        if (extra[i] != i + 1) break;
    printf("cve-2022-37434: a %d-byte gzip extra field in %d calls into a 16-byte "
           "buffer: %s, extra_len %u, first 16 bytes %s\n", XLEN, calls,
           r == Z_STREAM_END ? "decoded" : "not decoded", head.extra_len,
           i == 16 ? "kept" : "wrong");
    free(extra);
    return r == Z_STREAM_END && head.extra_len == XLEN && i == 16 ? 0 : 1;
}
