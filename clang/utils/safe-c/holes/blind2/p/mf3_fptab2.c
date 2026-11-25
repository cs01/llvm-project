// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
struct a{int x[5];}; struct o{int(*f)(struct a*);}; extern const struct o O; static int f(struct a*p){ return p->x[4]; } const struct o O={f};
