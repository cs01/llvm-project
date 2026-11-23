// safec.h redeclares libc's read-only string parameters as __null_terminated.
// The redeclaration must merge with the system one, and a buffer with known
// bounds passed to it goes through the checked conversion.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// expected-no-diagnostics

typedef __SIZE_TYPE__ size_t;
size_t strlen(const char *);
int strcmp(const char *, const char *);

size_t local(void) {
  char buf[8] = "hi";
  return strlen(buf) + (size_t)strcmp(buf, "hi");
}
