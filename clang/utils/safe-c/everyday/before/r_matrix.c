// opts:
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
static double tr(int n,double a[n][n]){ double s=0; for(int i=0;i<n;i++) s+=a[i][i]; return s; }
int main(void){ double a[2][2]={{1,0},{0,1}}; printf("%f\n",tr(2,a)); }
