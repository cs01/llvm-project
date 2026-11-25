// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct s{int*_Owned _Nullable p;}; typedef void(*cb)(struct s*_Exclusive); cb get(void); static void f(struct s*_Exclusive s){ free(_Take(s->p)); } cb get(void){ return f; }
