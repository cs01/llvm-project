#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <sys/sysctl.h>
#include <sys/wait.h>
#include <sys/event.h>
#include <sys/xattr.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <malloc/malloc.h>
#include <copyfile.h>
#include <zlib.h>
struct B{char b[4];char c[400];};NI int hh(void){return 0;}int main(int argc,char**argv){struct B s={"abc","CCCC"};volatile int n=10;char src[40]="xxxxxxxxxxxxxxxxxxxxxxxx";char nt[4]={1,1,1,1};volatile int m=100;char o[64]={0};struct{int a;int k[200];}q={1,{77}};(void)o;(void)src;(void)nt;(void)m; { mach_msg_type_number_t k=MACH_TASK_BASIC_INFO_COUNT;task_info(mach_task_self(),MACH_TASK_BASIC_INFO,&q.a,&k); } return s.c[0]!='C'||q.k[0]!=77?42:0;}
