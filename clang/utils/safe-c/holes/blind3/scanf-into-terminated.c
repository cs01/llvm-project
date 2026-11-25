#include <stdio.h>
#include <string.h>
static char g[__null_terminated 4] = "abc";
int main(void) { sscanf("xxxx", "%4c", g); return strlen(g); }
