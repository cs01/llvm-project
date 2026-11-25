#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
static int bind_in(int s,const struct sockaddr_in*a){ __unsafe { return bind(s,(const struct sockaddr*)a,sizeof *a); } }
int main(void){ int s=socket(AF_INET,SOCK_STREAM,0); struct sockaddr_in a={0}; a.sin_family=AF_INET; printf("%d\n",bind_in(s,&a)); }
