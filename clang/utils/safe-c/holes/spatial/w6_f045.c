#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
int main(void){_Atomic int*p=calloc(2,4);if(!p)return 1;p+=2;atomic_store(p,0x41);free(p-2);return 0;}
