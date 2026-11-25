// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
__attribute__((format(printf,1,2))) static int lg(const char*f,...){ va_list a; va_start(a,f); f++; int r=vprintf(f,a); va_end(a); return r;} 
int main(int c,char**v){ lg("%%s\n",4); }
