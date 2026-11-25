#include <stdio.h>
#include <pthread.h>
struct job{int in,out;};
_Trusted static void*_Nullable w(void*_Nullable a){ struct job*_Nullable __single j; __unsafe { j=a; } if(j) j->out=j->in*2; return NULL; }
_Trusted static int run(struct job*j){ pthread_t __single t; if(pthread_create(&t,NULL,w,j)) return 1; return pthread_join(t,NULL); }
int main(void){ struct job j={2,0}; if(run(&j)) return 1; printf("%d\n",j.out); }
