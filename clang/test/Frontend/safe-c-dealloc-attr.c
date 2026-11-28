// A system library's ownership contract read from its declarations: GCC's
// 'malloc(deallocator, index)', which glibc's headers use, and Clang's
// 'ownership_returns/takes'. What one function makes, only its own
// deallocator may free.
// RUN: rm -rf %t && split-file %s %t
// RUN: %clang -fsafe-c -fsyntax-only -isystem %t/inc -Xclang -verify %t/main.c

//--- inc/net.h
#pragma clang system_header
#include <ptrcheck.h>
__ptrcheck_abi_assume_single()
struct conn { int fd; };
void conn_close(int flags, struct conn *c);
struct conn *conn_open(const char *host) __attribute__((malloc(conn_close, 2)));

void buf_free(char *b) __attribute__((ownership_takes(bufs, 1)));
char *buf_new(void) __attribute__((ownership_returns(bufs)));

struct conn *conn_peek(void);

//--- main.c
#include <net.h>
#include <stdio.h>
#include <stdlib.h>
#include <safec.h>

int ok(void) {
  struct conn *_Owned c = conn_open("a");
  conn_close(0, c);
  char *_Owned b = buf_new();
  buf_free(b);
  return 0;
}

int leak(void) {
  struct conn *_Owned c = conn_open("a");
  return 0; // expected-error {{'c' still owns memory when the function returns}}
}

int twice(void) {
  struct conn *_Owned c = conn_open("a");
  conn_close(0, c);
  conn_close(0, c); // expected-error {{'c' is closed after it was closed on line}}
  return 0;
}

int wrong_free(void) {
  struct conn *_Owned c = conn_open("a");
  free(c); // expected-error {{the library made this and only its own function closes it}}
  return 0;
}

int wrong_family(void) {
  char *_Owned b = buf_new();
  free(b); // expected-error {{this was made by 'buf_new', so 'bufs' must free it, not 'free'}}
  return 0;
}

int no_contract(void) {
  struct conn *c = conn_peek(); // expected-error {{'conn_peek' has no ownership contract}}
  return 0;
}

// The table pairs what Apple's headers do not say: fopen with fclose.
int fopen_pclose(void) {
  FILE *_Owned f = fopen("x", "r");
  if (!f)
    return 1;
  pclose(f); // expected-error {{this was made by 'fopen', so 'fclose' must free it, not 'pclose'}}
  return 0;
}

// Under Safe C the attribute is used, so it is not "ignored".
void my_free(char *p);
char *my_alloc(void) __attribute__((malloc(my_free, 1)));
