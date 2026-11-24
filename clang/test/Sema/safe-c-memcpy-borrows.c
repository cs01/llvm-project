// Fuzzing hole 15 (2026-11-23): memcpy(d, s, n) copies the borrows *s holds
// into *d, as `*d = *s` would, so a byte copy cannot launder a loan. The
// control run is plain C (safec.h's annotations expand to nothing).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *memcpy(void *, const void *, size_t);
void *_Nullable malloc(size_t);
void free(void *_Nullable);
struct _View V { int *_Nullable p; };

int view_copy(void) {
  struct V out = {0};
  int *_Nullable h = malloc(4); // expected-error {{object allocated by 'malloc' does not live long enough}}
  if (!h)
    return 1;
  struct V in = {h};            // expected-note {{aliases}}
  memcpy(&out, &in, sizeof out);
  free(h);                      // expected-note {{freed here}}
  return out.p ? *out.p : 0;    // expected-note {{later used here}}
}

int stack_copy(void) {
  int y = 1;
  int *out = &y;
  {
    int x = 42;
    int *in = &x;               // expected-error {{local variable 'x' does not live long enough}}
    memcpy(&out, &in, sizeof out);
  }                             // expected-note {{destroyed here}}
  return *out;                  // expected-note {{later used here}}
}

int fine(void) {
  int y = 1, z = 2;
  int *out = &y, *in = &z;
  memcpy(&out, &in, sizeof out);
  int buf[4] = {0}, src[4] = {1, 2, 3, 4};
  memcpy(buf, src, sizeof buf);
  return *out + buf[1];
}
