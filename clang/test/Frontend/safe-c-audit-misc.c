// Holes from the 2026-11-23 audit, through the driver with real headers:
// a noreturn function that returns, asm labels (bind a declaration to any
// symbol of any type), setjmp/longjmp (resume a dead frame), and use of a
// FILE after fclose.
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsyntax-only -Xclang -verify=plain %s
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

__attribute__((noreturn)) void die(int c) {
  if (c)
    exit(1);
} // expected-error {{function declared 'noreturn' should not return}} plain-warning {{function declared 'noreturn' should not return}}

extern int *view __asm__("_cell"); // expected-error {{an asm label can switch off Safe C's checks}}

_Thread_local jmp_buf jb;
void jump(void) {
  longjmp(jb, 1); // expected-error {{'longjmp' can switch off Safe C's checks}}
}

void free_local(void) {
  int x = 0;
  free(&x); // expected-error {{attempt to call free on non-heap object 'x'}}  plain-warning {{attempt to call free on non-heap object 'x'}}
}

void drop(int *_Nullable p) __attribute__((ownership_takes(malloc, 1)));
void free_local_via_owner(void) {
  int arr[4] = {0};
  drop(arr); // expected-error {{attempt to call drop on non-heap object 'arr'}}
}

int closed_twice(void) {
  FILE *_Nullable f = fopen("/dev/null", "r");
  if (!f)
    return 0;
  fclose(f);
  return fgetc(f);
}
