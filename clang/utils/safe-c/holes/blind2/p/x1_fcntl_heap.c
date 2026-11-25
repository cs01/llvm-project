// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <fcntl.h>
int main(int c,char**v){ char*p=malloc(1); if(!p) return 1; int f=open("/etc/hosts",O_RDONLY); fcntl(f,F_GETPATH,p); printf("%c\n",p[0]); free(p); }
