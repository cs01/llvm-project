// alloca(n) under Safe C: its result carries n bytes of bounds (so indexing
// is checked and the cast from void * is allowed, like malloc's), and the
// block lives in this frame: returning it is a dangling stack pointer, and it
// is not memory to free. The control run is plain C.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=off %s
// off-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *alloca(size_t);
void free(void *_Nullable);

int use(void) {
  char *buf = (char *)alloca(10);
  buf[0] = 'a';
  return buf[0];
}

char *escape(void) {
  char *p = (char *)alloca(4); // expected-error {{stack memory associated with result of call to 'alloca' is returned}}
  return p;                    // expected-note {{returned here}}
}
