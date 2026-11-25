// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
typedef void (*cb)(int*_Owned); extern cb get(void); static void f(int*_Owned p){ free(p);} cb get(void){ return f; }
