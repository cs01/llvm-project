#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{int k;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}u;};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){int x=1;struct S s={.k=0,.u={.l=BAD}};struct S o={.k=1,.u={.p=&x}};struct S*a=&o;struct S*b=&o;volatile int one=1;(void)a;(void)b;(void)one; struct S t[2]={[0]=s,[0].k=1};struct S*e=t;if(e->k==1)USE(e->u.p) ;return 0;}
