// opts: std=c23
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
enum e:long{A,B}; void f(enum e*x); void f(enum e*x){ *x=B; }
