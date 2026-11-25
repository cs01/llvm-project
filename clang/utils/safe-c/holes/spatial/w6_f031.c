#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
int main(void){_Atomic int a[2]={0};_Atomic int*p=a;p+=2;atomic_store(p,0x41);return 0;}
