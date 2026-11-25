#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#define NI __attribute__((noinline))

struct W{wchar_t w[2];wchar_t s[4];};struct B{char b[4];char c[40];};NI int hh(void){return 0;}int main(int argc,char**argv){struct B s={"abc","CCCC"};struct W w={{65,65},{'S','E','C',0}};volatile int n=10;char src[40]="xxxxxxxxxxxxxxxxxxxxxxxx";char nt[4]={1,1,1,1};volatile int m=100;char o[64]={0};(void)o;(void)src;(void)nt;(void)m; { const wchar_t*p=w.w;int r=snprintf(o,64,"%ls",p);return r==5?42:0; } return s.c[0]!='C'?42:0;}
