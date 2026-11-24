// The byte-write rule through the driver with the real libc headers, where
// memcpy can be a fortify or bounds-safety wrapper (__memcpy_ptrchk,
// __builtin___memcpy_chk). A test that declares its own memcpy missed that
// (found by the 2026-11-23 audit, probes y2_memcpy_forge, w09a-c, v08).
// RUN: %clang -fsafe-c -fsyntax-only -Xclang -verify %s
// RUN: %clang -fsafe-c -fsyntax-only -O2 -D_FORTIFY_SOURCE=2 -Xclang -verify %s
// RUN: %clang -fsyntax-only -Xclang -verify=plain %s
// plain-no-diagnostics
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

struct H { int *p; }; // expected-error {{holds a pointer nothing tracks}}
struct Bytes { long a[2]; };
void my_free(void *p) __attribute__((ownership_takes(malloc, 1)));

void f(struct H *h, struct H *other, struct Bytes *b, long *raw, int fd) {
  memcpy(h, other, sizeof *h);
  memset(h, 0, sizeof *h);
  bzero(h, sizeof *h);
  memcpy(b, raw, sizeof *raw);
  memcpy(h, raw, sizeof *h);        // expected-error {{copying 'long' into 'struct H', which holds pointers, can forge a pointer}}
  memmove(h, raw, sizeof *h);       // expected-error {{copying 'long' into 'struct H'}}
  memset(h, 0x41, sizeof *h);       // expected-error {{filling 'struct H', which holds pointers, can forge a pointer; fill with 0}}
  bcopy(raw, h, sizeof *h);         // expected-error {{copying 'long' into 'struct H'}}
  (void)read(fd, h, sizeof *h);     // expected-error {{reading bytes into 'struct H', which holds pointers}}
#ifdef __APPLE__
  arc4random_buf(h, sizeof *h);     // expected-error {{reading bytes into 'struct H', which holds pointers}}
#endif
  my_free(other); // freed, not written: no byte-write error expected-error {{'f' frees its parameter 'other'}}
  free(h); // expected-error {{'f' frees its parameter 'h'}}
}
