// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct c{int z[3];}; struct b{const struct c*_Static p;}; void f(const struct b*s); void f(const struct b*s){ printf("%d\n",s->p->z[2]); }
