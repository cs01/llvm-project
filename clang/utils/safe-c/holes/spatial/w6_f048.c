#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
int main(void){_Atomic int a[2]={0};int e[2]={0,0};int*x=e;x+=2;atomic_compare_exchange_strong(&a[0],x,1);return 0;}
