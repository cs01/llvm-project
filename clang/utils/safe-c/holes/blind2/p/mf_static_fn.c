// opts: +mf_static_fn2
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef void (*cb)(int*); extern cb get(void); int main(void){ int*p=malloc(4); if(!p) return 1; get()(p); *p=1; }
