/* inih -- tests with custom memory allocator */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "../ini.h"

/* The annotations repeat ini.c's declarations of these three. */
 __attribute__((alloc_size(1)))
void* _Owned _Nullable __sized_by_or_null(size) ini_malloc(size_t size) {
    printf("ini_malloc(%d)\n", (int)size);
    return malloc(size);
}

void ini_free(void* _Nullable _Owned ptr) {
    printf("ini_free()\n");
    free(ptr);
}

 __attribute__((alloc_size(2)))
void* _Owned _Nullable __sized_by_or_null(size) ini_realloc(void* _Nullable _Owned ptr,
                                                     size_t size) {
    printf("ini_realloc(%d)\n", (int)size);
    /* unsafe: alloc_size promises zeroed memory, which realloc's growth is
       not; ini.c only ever holds text in it */
    __unsafe { return realloc(ptr, size); }
}

/* Safe C: _Thread_local, as a mutable global shared by threads is a race. */
_Thread_local char Prev_section[50];

int dumper(void* _Nullable user, const char* section _Noescape,
           const char* name _Noescape, const char* value _Noescape)
{
    if (strcmp(section, Prev_section)) {
        printf("... [%s]\n", section);
        strlcpy(Prev_section, section, sizeof(Prev_section));
    }
    printf("... %s=%s;\n", name, value);
    return 1;
}

void parse(const char* name, const char* string) {
    int e;

    *Prev_section = '\0';
    e = ini_parse_string(string, dumper, NULL);
    printf("%s: e=%d\n", name, e);
}

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    parse("basic", "[section]\nfoo = bar\nbazz = buzz quxx");
    return 0;
}
