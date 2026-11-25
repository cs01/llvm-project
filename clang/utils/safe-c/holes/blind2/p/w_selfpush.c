// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "safec_std.h"
typedef VEC(int) iv; 
int main(void){ iv v={0}; for(int i=0;i<4;i++) vec_push(&v,i); if(!v.items) return 1; int*x=&v.items[0]; vec_push(&v,*x); printf("%d\n",v.items?v.items[4]:0); vec_free(&v); }
