// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(void){ char*line=NULL; size_t cap=0; ssize_t n; while((n=getline(&line,&cap,stdin))>0) printf("%zd\n",n); free(line); }
