/* inih -- tests for ini_parse_string() */

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
   because every ini_parse_string() call in this file passes &u, an int. */
static int user_value(void* _Nullable user)
{
    int v = 0;
    __unsafe {
        v = user ? *((int*)user) : 0;
    }
    return v;
}

int dumper(void* _Nullable user, const char* section _Noescape,
           const char* name _Noescape, const char* value _Noescape)
{
    User = user_value(user);
    if (strcmp(section, Prev_section)) {
        printf("... [%s]\n", section);
        strlcpy(Prev_section, section, sizeof(Prev_section));
    }
    printf("... %s=%s;\n", name, value);
    return 1;
}

void parse(const char* name, const char* string) {
    static _Thread_local int u = 100;
    int e;

    *Prev_section = '\0';
    e = ini_parse_string(string, dumper, &u);
    printf("%s: e=%d user=%d\n", name, e, User);
    u++;
}

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    parse("empty string", "");
    parse("basic", "[section]\nfoo = bar\nbazz = buzz quxx");
    parse("crlf", "[section]\r\nhello = world\r\nforty_two = 42\r\n");
    parse("long line", "[sec]\nfoo = 01234567890123456789\nbar=4321\n");
    parse("long continued", "[sec]\nfoo = 0123456789012bix=1234\n");
    parse("error", "[s]\na=1\nb\nc=3");
    return 0;
}
