/* inih -- tests for file read errors */

#include <stdio.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "../ini.h"

int handler(void* _Nullable user, const char* section _Noescape,
            const char* name _Noescape, const char* value _Noescape)
{
    (void)user;
    (void)section;
    (void)name;
    (void)value;
    return 1;
}

int main(void)
{
    const char* filename = "unittest_file_error.tmp";
    FILE* _Owned _Nullable file;
    int e;

#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    /* Reading from a write-only stream sets the stream's error indicator. */
    file = fopen(filename, "w");
    if (!file) {
        printf("unable to open %s\n", filename);
        return 1;
    }
    e = ini_parse_file(file, handler, NULL);
    printf("read_error: e=%d ferror=%d\n", e, ferror(file) != 0);
    fclose(file);
    remove(filename);
    return 0;
}
