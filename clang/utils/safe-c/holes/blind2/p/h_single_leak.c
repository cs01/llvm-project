// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static void f(char*p){ printf("%s\n",p); }
int main(int c,char**v){ struct{char b; char s[4];}x={'a',"SEC"}; f(&x.b); }
