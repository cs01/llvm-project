/* inih -- tests

This works simply by dumping a bunch of info to standard output, which is
redirected to an output file (baseline_*.txt) and checked into the Git
repository. This baseline file is the test output, so the idea is to check it
once, and if it changes -- look at the diff and see which tests failed.

See unittest.bat and unittest.sh for how to run this (with tcc and gcc,
respectively).

*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
#include "../ini.h"

/* Safe C: _Thread_local, as a mutable global shared by threads is a race. */
_Thread_local int User;
_Thread_local char Prev_section[50];

/* Safe C: the handler is handed the int that parse() passed as void*, but
   the compiler cannot see that, so it rejects the cast back to int*. Safe
   because every ini_parse() call in this file passes &u, an int. */
_Trusted static int user_value(void* _Nullable user)
{
    int v = 0;
    __unsafe {
        v = user ? *((int*)user) : 0;
    }
    return v;
}

#if INI_HANDLER_LINENO
int dumper(void* _Nullable user, const char* section _Noescape,
           const char* INI_NAME_NULLABLE name _Noescape,
           const char* INI_VALUE_NULLABLE value _Noescape, int lineno)
#else
int dumper(void* _Nullable user, const char* section _Noescape,
           const char* INI_NAME_NULLABLE name _Noescape,
           const char* INI_VALUE_NULLABLE value _Noescape)
#endif
{
    User = user_value(user);
    if (!name || strcmp(section, Prev_section)) {
        printf("... [%s]\n", section);
        strlcpy(Prev_section, section, sizeof(Prev_section));
    }
    if (!name) {
        return 1;
    }

#if INI_HANDLER_LINENO
    printf("... %s%s%s;  line %d\n", name, value ? "=" : "", value ? value : "", lineno);
#else
    printf("... %s%s%s;\n", name, value ? "=" : "", value ? value : "");
#endif

    if (!value) {
        /* Happens when INI_ALLOW_NO_VALUE=1 and line has no value (no '=' or ':') */
        return 1;
    }

    return strcmp(name, "user") == 0 && strcmp(value, "parse_error") == 0 ? 0 : 1;
}

void parse(const char* fname) {
    static _Thread_local int u = 100;
    int e;

    *Prev_section = '\0';
    e = ini_parse(fname, dumper, &u);
    printf("%s: e=%d user=%d\n", fname, e, User);
    u++;
}

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    parse("no_file.ini");
    parse("normal.ini");
    parse("bad_section.ini");
    parse("bad_comment.ini");
    parse("user_error.ini");
    parse("multi_line.ini");
    parse("bad_multi.ini");
    parse("bom.ini");
    parse("duplicate_sections.ini");
    parse("no_value.ini");
    parse("long_section.ini");
    parse("long_line.ini");
    parse("name_only_after_error.ini");
    return 0;
}
