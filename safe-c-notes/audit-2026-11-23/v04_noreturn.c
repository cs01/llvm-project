#include <stdio.h>
#include <stdlib.h>
__attribute__((noreturn, noinline)) static void die(int x) { if (x) exit(1); }
__attribute__((noinline)) static void gadget(void) { puts("fell through into gadget"); exit(0); }
int main(int argc, char **argv) { die(argc - 1); }
void *keep = (void *)0;
void use(void) { gadget(); }
