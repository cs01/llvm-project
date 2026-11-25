#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
union _Tagged_by(k) U{long l _When(0);int*_Nullable p _When(1);};struct S{int k;union U u;};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){static int x=1; struct S s={.k=0,.u={.l=BAD}};union U u=s.u;USE(u.p) ;return 0;}
