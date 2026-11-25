#include <stdio.h>
#include <pthread.h>
struct job{int in,out;};
static void*_Nullable w(void*_Nullable a){ struct job*_Nullable __single j; __unsafe { j=a; } if(j) j->out=j->in*2; return NULL; }
int main(void){ struct job j={2,0}; pthread_t __single t; if(pthread_create(&t,NULL,w,&j)) return 1; pthread_join(t,NULL); printf("%d\n",j.out); }
