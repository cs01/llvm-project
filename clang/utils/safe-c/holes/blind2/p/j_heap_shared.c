// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
struct s{int a;};
static _Atomic(struct s*_Static _Nullable) G; static _Thread_local struct s T;
static void*_Nullable w(void*_Nullable a){ struct s*_Nullable p=G; if(p) p->a++; return NULL; }
int main(void){ pthread_t __single t; G=&T; pthread_create(&t,NULL,w,NULL); T.a=4; pthread_join(t,NULL); }
