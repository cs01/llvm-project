// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sys/socket.h>
int main(int c,char**v){ struct{struct sockaddr a; int z[8];}x={{0},{77,77,77,77}}; socklen_t n=48; int s=socket(AF_INET6,SOCK_DGRAM,0); printf("%d ",getsockname(s,&x.a,&n)); printf("%d %d\n",(int)n,x.z[0]); }
