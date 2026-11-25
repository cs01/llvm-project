#include <stdio.h>
#include <stdlib.h>
int main(void){ char*_Owned __null_terminated _Nullable s=NULL; if(asprintf(&s,"%d",4)>0&&s) puts(s); free(s); }
