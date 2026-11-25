#include <stdio.h>
#include <string.h>
int main(void){ const char s[]="a b c"; size_t i=0; while(i<sizeof s-1){ char t[8]; size_t n=0; while(i<sizeof s-1&&s[i]!=' '&&n<sizeof t-1) t[n++]=s[i++]; t[n]=0; i++; printf("%s\n",t); } }
