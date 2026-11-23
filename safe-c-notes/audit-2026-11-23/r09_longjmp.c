#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
jmp_buf jb;
__attribute__((noinline)) static void arm(void) { volatile long local = 5; if (setjmp(jb)) { printf("resumed in dead frame, local=%lx\n", local); exit(0); } }
__attribute__((noinline)) static void smash(void) { volatile long a[64]; for (int i = 0; i < 64; i++) a[i] = 0x4949494949494949; longjmp(jb, 1); }
int main(void) { arm(); smash(); return 0; }
