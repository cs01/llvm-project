// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
static _Atomic int g; 
static void*_Nullable w(void*_Nullable a){ for(int i=0;i<20000;i++) atomic_init(&g,i); return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); w(NULL); pthread_join(t,NULL); }
