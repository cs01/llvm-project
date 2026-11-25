// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
int main(int c,char**v){ struct{char b[2]; char s[4];}x={{'a','b'},"SEC"}; printf("a%s\n"+1,x.b); }
