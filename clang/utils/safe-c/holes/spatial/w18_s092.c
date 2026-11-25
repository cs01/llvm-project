#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{int k;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}u[2];};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){static int x=1;volatile int one=1;int*_Nullable nn=&x; struct S s={.k=1,.u={{.p=0},{.l=BAD}}};if(s.k==1)USE(s.u[1].p) ;return 0;}
