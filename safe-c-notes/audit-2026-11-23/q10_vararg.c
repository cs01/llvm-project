#include <stdarg.h>
#include <stdio.h>
static long sum(int n, ...) { va_list ap; va_start(ap, n); long s = 0; for (int i = 0; i < n; i++) s += va_arg(ap, long); va_end(ap); return s; }
int main(void) { printf("%lx\n", sum(40, 1L)); return 0; }
