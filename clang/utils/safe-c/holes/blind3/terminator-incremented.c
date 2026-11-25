#include <string.h>
static unsigned long f(char *__null_terminated s _Noescape) { while (*s) s++; ++*s; return strlen(s); }
int main(void) { char b[4] = "abc"; return f(b); }
