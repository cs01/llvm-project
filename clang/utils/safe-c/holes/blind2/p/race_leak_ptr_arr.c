// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER; static int h[4] _Guarded_by(m);
static void*_Nullable w(void*_Nullable a){ int*p; pthread_mutex_lock(&m); p=h; pthread_mutex_unlock(&m); p[1]++; return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); w(NULL); pthread_join(t,NULL); }
