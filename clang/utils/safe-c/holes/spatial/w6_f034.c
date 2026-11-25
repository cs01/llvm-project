#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct S{_Atomic int a[2];int k;};int main(void){struct S s={{0},7};volatile int i=2;atomic_store(s.a+i,0x41);return s.k==0x41?42:0;}
