// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <dirent.h>
int main(void){ DIR*d=opendir("/etc"); if(!d) return 1; struct dirent*e; int n=0; while((e=readdir(d))) n++; closedir(d); printf("%d\n",n>0); }
