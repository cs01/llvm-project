#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#define BAD 0x4141414141414148
struct S{int k;union _Tagged_by(k){long l _When(0);int*_Nullable p _When(1);}u;};
#define USE(e) do{int*_Nullable q_=(e);if(q_)printf("%d",*q_);}while(0)
NI void poison(struct S*z){z->k=0;z->u.l=BAD;} NI int*_Nullable get(struct S*z){if(z->k==1)return z->u.p;return 0;} NI int rd(struct S*z){if(z->k==1)USE(z->u.p);return 0;}
static struct S gs; static struct S*gp=&gs; NI void pg(void){gs.k=0;gs.u.l=BAD;} static int gx;
int main(void){static int x=1;struct S s={.k=0,.u={.l=BAD}};struct S o={.k=1,.u={.p=&x}};struct S*a=&o;volatile int one=1;(void)a;(void)one;gs.k=1;gs.u.p=&gx; rd(&(struct S){.k=1,.u={.l=BAD}}) ;return 0;}
