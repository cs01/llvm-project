#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{int k;union _Tagged_by(k){long l _When(0);int(*_Nullable f)(int) _When(1);}u;};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
int main(void){static int x=1;volatile int one=1;int*_Nullable nn=&x; struct S t[1]={{1,{BAD}}};struct S*e=t;if(e->k==1&&e->u.f)e->u.f(1) ;return 0;}
