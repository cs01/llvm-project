#include <stdio.h>
#include <dirent.h>
_Trusted static struct dirent*_Nullable __single next(DIR*__single d){ struct dirent*_Nullable __single e; __unsafe { e=readdir(d); } return e; }
int main(void){ DIR*_Owned _Nullable __single d=opendir("/etc"); if(!d) return 1; int n=0; while(next(d)) n++; closedir(d); printf("%d\n",n>0); }
