// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
int main(int c,char**v){ struct{char p[1]; char sec[7];}x={{0},"abcdef"}; int f=open("/etc/hosts",O_RDONLY); long r=syscall(3,f,&x.p,8L); printf("%ld %s\n",r,x.sec); }
