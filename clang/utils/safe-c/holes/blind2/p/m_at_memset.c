// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
static _Atomic unsigned g[64]; 
static void*_Nullable w(void*_Nullable a){ for(int i=0;i<2000000;i++) memset(g,i,sizeof g); return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); unsigned s=0; for(int i=0;i<2000000;i++) s+=g[i&63]; pthread_join(t,NULL); printf("%u\n",s); }
