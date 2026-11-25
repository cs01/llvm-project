// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
struct s{int a[2000];};
static _Atomic(struct s*_Static _Nullable) G; static _Thread_local struct s T;
static void*_Nullable w(void*_Nullable a){ T.a[5]=3; G=&T; return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); pthread_join(t,NULL); struct s*_Nullable p=G; if(p) printf("%d\n",p->a[5]); }
