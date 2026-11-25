// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <signal.h>
static volatile sig_atomic_t stop; static void h(int s){ stop=1; }
int main(void){ struct sigaction sa={0}; sa.sa_handler=h; sigaction(SIGINT,&sa,NULL); printf("%d\n",stop); }
