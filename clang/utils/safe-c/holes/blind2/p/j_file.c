// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
static void*_Nullable w(void*_Nullable a){ for(int i=0;i<200;i++) putc_unlocked('a',stdout); return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); w(NULL); pthread_join(t,NULL); }
