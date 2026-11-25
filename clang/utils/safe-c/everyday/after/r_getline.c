#include <stdio.h>
#include <stdlib.h>
int main(void){ char line[256]; while(fgets(line,sizeof line,stdin)) printf("line\n"); }
