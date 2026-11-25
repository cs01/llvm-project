#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
NI int f(int*__counted_by(n)d,int n){_Atomic int x=99;atomic_compare_exchange_strong(&x,&n,1);return d[4];}int main(void){int a[2]={1};return f(a,2);}
