// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
struct b{int n; int p[__counted_by(n)];};
static int f(struct b*s,int fd){ if(read(fd,s,2)!=2) return 0; return s->p[200]; }
int main(int c,char**v){ struct b*s=malloc(8); if(!s) return 1; s->n=1; int fd=open("/etc/hosts",O_RDONLY); printf("%d\n",f(s,fd)); }
