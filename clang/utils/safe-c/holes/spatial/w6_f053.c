#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <stdatomic.h>
struct V{int n;int*__counted_by(n)_Nullable d;};int main(void){int a[2]={1};struct V v={2,a};_Atomic int x=99;atomic_compare_exchange_strong(&x,&v.n,1);return v.d?v.d[4]:0;}
