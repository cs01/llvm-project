// Under -fsafe-c, raw byte writes (memcpy, memset, read, ...) into memory that
// holds pointers must not forge one: copy from the same type, fill with zero,
// or read into a pointer-free buffer.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *memset(void *, int, size_t);
typedef struct FILE FILE;
size_t fread(void *, size_t, size_t, FILE *);

struct S { int *p; };
struct Bytes { long a[2]; };

void f(struct S *s, struct S *t, struct Bytes *b, long *l, FILE *fp) {
  memcpy(s, t, sizeof *s);
  memset(s, 0, sizeof *s);
  memcpy(b, l, sizeof *l);
  fread(b, sizeof *b, 1, fp);
  memcpy(s, l, sizeof *s);  // expected-error {{copying 'long' into 'struct S', which holds pointers, can forge a pointer; copy from the same type}}
  memset(s, 0x41, sizeof *s); // expected-error {{filling 'struct S', which holds pointers, can forge a pointer; fill with 0}}
  fread(s, sizeof *s, 1, fp); // expected-error {{reading bytes into 'struct S', which holds pointers}}
}
