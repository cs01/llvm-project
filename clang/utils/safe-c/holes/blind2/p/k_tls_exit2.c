// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
static _Atomic(int*) G; 
static void*_Nullable w(void*_Nullable a){ int z=4; G=&z; return NULL; }
int main(void){ pthread_t __single t; pthread_create(&t,NULL,w,NULL); pthread_join(t,NULL); int*__single p=G; printf("%d\n",*p); }
