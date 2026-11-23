#include <setjmp.h>
#include <stdio.h>
jmp_buf jb;
static void arm(void) { int local = 5; if (setjmp(jb)) { printf("resumed in dead frame %d\n", local); } }
static void smash(void) { volatile long a[64]; for (int i = 0; i < 64; i++) a[i] = 0x4949494949494949; }
int main(void) { arm(); smash(); longjmp(jb, 1); return 0; }
