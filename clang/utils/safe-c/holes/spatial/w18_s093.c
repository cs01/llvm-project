#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{int k;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}u;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}v;};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){static int x=1;volatile int one=1;int*_Nullable nn=&x; struct S s={.k=0,.u={.l=BAD},.v={.l=BAD}};s.k=1;s.u.p=0;if(s.k==1)USE(s.v.p) ;return 0;}
