#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct L{int a[8];};int main(void){_Atomic struct L l[1];struct L d={0};atomic_init(&l[0],d);_Atomic struct L*p=l;p+=1;struct L k=atomic_load(p);return k.a[3];}
