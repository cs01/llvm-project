#include <sys/syscall.h>
#include <unistd.h>
int main(void){struct{char b[4];char c[4];}s={{0},{1}};int p[2];pipe(p);write(p[1],"AAAAAAAA",8);syscall(SYS_read,p[0],s.b,8);return s.c[0]=='A'?42:0;}
