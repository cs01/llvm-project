#include <string.h>
union u { char g[__null_terminated 4]; int i; };
int main(void) { union u x = {"abc"}; x.i = 0x41414141; return strlen(x.g); }
