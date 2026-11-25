// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
__attribute__((format(printf,1,2))) static int lg(const char*f,...){ va_list a; va_start(a,f); int r=vprintf(f,a); va_end(a); return r;} 
int main(void){ __attribute__((format(printf,1,2))) int(*p)(const char*,...)=lg; p("%s\n","x"); int(*q)(const char*,...)=lg; q("%s %s\n",3); }
