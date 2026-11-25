// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct o{ int(*f)(int*,int n); }; void g(struct o*o); void g(struct o*o){ int z=1; printf("%d\n",o->f(&z,3)); }
