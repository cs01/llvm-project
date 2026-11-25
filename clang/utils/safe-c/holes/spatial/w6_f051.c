#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct S{int e[2];int k;};int main(void){_Atomic int a=9;struct S s={{0},7};volatile int i=2;atomic_compare_exchange_weak_explicit(&a,s.e+i,1,memory_order_seq_cst,memory_order_seq_cst);return s.k==9?42:0;}
