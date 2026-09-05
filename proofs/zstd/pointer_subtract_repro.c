#include <stdio.h>
#include <string.h>
#include <stddef.h>
static char dst_buf[64];
static char src_buf[64];
int main(void) {
    // Exactly what ZSTD_wildcopy does in the ZSTD_no_overlap case: subtract two
    // pointers that the caller guarantees are in *different* objects.
    char *dst = dst_buf;
    const char *src = src_buf;
    ptrdiff_t diff = (char *)dst - (const char *)src;
    printf("diff computed: %ld\n", (long)diff);
    return 0;
}
