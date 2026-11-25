#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{struct{int k;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}u;}w[2];};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){static int x=1;volatile int one=1;int*_Nullable nn=&x; struct S s={.w={{.k=0,.u={.l=BAD}}}};typeof(s.w[0])*e=s.w;e->k=1;e->u.p=0;if(e->k==1){s.w[0]=s.w[1];s.w[0]=(typeof(s.w[0])){.k=0,.u={.l=BAD}};USE(e->u.p);} ;return 0;}
