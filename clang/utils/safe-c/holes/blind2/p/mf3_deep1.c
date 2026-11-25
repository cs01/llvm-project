// opts: +mf3_deep12
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct c{int z;}; struct b{const struct c*_Static p;}; void f(const struct b*s); static const struct c C={1}; static const struct b B={&C}; int main(void){ f(&B); }
