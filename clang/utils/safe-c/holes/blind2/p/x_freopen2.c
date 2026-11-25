// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(void){ FILE*_Nullable f=fopen("/etc/hosts","r"); if(!f) return 1; FILE*_Nullable g=freopen("/etc/hosts","r",f); if(!g) return 1; fclose(f); FILE*_Nullable h=fopen("/etc/passwd","r"); if(!h) return 1; char b[64]; if(fgets(b,64,g)) printf("%s\n",b); fclose(h); }
