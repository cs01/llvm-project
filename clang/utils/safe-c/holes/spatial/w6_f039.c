#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct S{_Atomic int a[2];int k;};int main(void){struct S s={{0},7};_Atomic int*p=s.a;p=p+2;int x=atomic_fetch_add(p,3);return x==7?42:0;}
