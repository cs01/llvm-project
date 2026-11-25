#include <stdio.h>
static double tr(int n,const double*__counted_by(n*n) a){ double s=0; for(int i=0;i<n;i++) s+=a[i*n+i]; return s; }
int main(void){ double a[4]={1,0,0,1}; printf("%f\n",tr(2,a)); }
