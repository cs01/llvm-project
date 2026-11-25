#include <string.h>
static unsigned long f(const char *s _Noescape, char *__counted_by(4) b) { b[3] = 'x'; return strlen(s); }
int main(void) { char b[4] = "abc"; return f(b, b); }
