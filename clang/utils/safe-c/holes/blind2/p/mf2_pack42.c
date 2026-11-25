// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{char a; _Alignas(32) long v[4]; }; void f(struct s*s); void f(struct s*s){ s->v[3]=7; }
