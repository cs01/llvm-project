#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <syslog.h>
#include <err.h>
#include <semaphore.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>
struct B{char b[4];char c[400];};NI int hh(void){return 0;}int main(int argc,char**argv){struct B s={"abc","CCCC"};volatile int n=10;char src[40]="xxxxxxxxxxxxxxxxxxxxxxxx";char nt[4]={1,1,1,1};volatile int m=100;char o[64]={0};struct{int a;int k[200];}q={1,{77}};(void)o;(void)src;(void)nt;(void)m; { int p[2];pipe(p);write(p[1],src,20);syscall(SYS_read,p[0],s.b,20); } return s.c[0]!='C'||q.k[0]!=77?42:0;}
