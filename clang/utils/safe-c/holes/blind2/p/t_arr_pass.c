// opts: tsan
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
struct _Capability("mutex") mtx { pthread_mutex_t m; };
static struct mtx m={PTHREAD_MUTEX_INITIALIZER};
static int h[4] _Guarded_by(m);
static void inc(int*__counted_by(n) p,int n){ for(int i=0;i<n;i++) p[i]++; }
static void*_Nullable w(void*_Nullable a){ inc(h,4); return NULL; }
int main(void){ pthread_t __single t; if(pthread_create(&t,NULL,w,NULL)) return 1; w(NULL); pthread_join(t,NULL); }
