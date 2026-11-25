// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static int count(const char*s){ int n=0; for(int i=0;s[i];i++) if(s[i]==' ') n++; return n; }
int main(void){ printf("%d\n",count("a b")); }
