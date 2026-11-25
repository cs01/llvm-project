// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
static pthread_mutex_t m=PTHREAD_MUTEX_INITIALIZER; static int n _Guarded_by(m);
int main(void){ pthread_mutex_lock(&m); n++; pthread_mutex_unlock(&m); }
