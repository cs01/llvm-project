// opts: +mf2_pack42
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{char a; long v[4]; }; void f(struct s*s); int main(void){ struct s*a=calloc(1,sizeof*a); if(!a) return 1; f(a); free(a); }
