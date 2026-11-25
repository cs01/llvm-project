// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
static _Atomic(int*) G; static _Thread_local int T;
static void*_Nullable w(void*_Nullable a){ int*__single p=G; (*p)++; return NULL; }
int main(void){ pthread_t __single t; G=&T; pthread_create(&t,NULL,w,NULL); T=4; pthread_join(t,NULL); }
