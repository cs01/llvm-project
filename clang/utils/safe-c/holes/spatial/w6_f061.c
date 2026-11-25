#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
int main(void){_Atomic int a[2][2]={0};volatile int i=2;atomic_store(a[0]+i,5);return a[1][0]==5?42:0;}
