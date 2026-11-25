#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct S{_Atomic int a[2];int k;};int main(void){struct S s={{0},7};_Atomic int*p=s.a;p++;p++;int x=atomic_load(p);return x==7?42:0;}
