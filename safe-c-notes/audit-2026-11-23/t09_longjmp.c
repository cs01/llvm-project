#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
jmp_buf jb;
__attribute__((noinline)) static void arm(void) {
  volatile long local[8]; local[3] = 5;
  if (setjmp(jb)) { fprintf(stderr, "resumed in dead frame, local=%lx\n", local[3]); exit(0); }
}
__attribute__((noinline)) static void smash(void) {
  volatile long a[64]; for (int i = 0; i < 64; i++) a[i] = 0x4949494949494949;
  fprintf(stderr, "jumping\n");
  longjmp(jb, 1);
}
int main(void) { arm(); smash(); return 0; }
