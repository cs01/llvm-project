#include <stdio.h>
#include <signal.h>
static volatile sig_atomic_t stop; static void h(int s){ stop=1; }
int main(void){ signal(SIGINT,h); printf("%d\n",stop); }
