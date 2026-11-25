// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{unsigned a:30; unsigned b:30; }; void f(struct s*s); void f(struct s*s){ s->b=7; }
