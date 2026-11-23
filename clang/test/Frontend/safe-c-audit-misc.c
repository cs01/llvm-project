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

jmp_buf jb;
void jump(void) {
  longjmp(jb, 1); // expected-error {{'longjmp' can switch off Safe C's checks}}
}

int closed_twice(void) {
  FILE *_Nullable f = fopen("/dev/null", "r"); // expected-error {{does not live long enough}}
  if (!f)
    return 0;
  fclose(f);   // expected-note {{freed here}}
  return fgetc(f); // expected-note {{later used here}}
}
