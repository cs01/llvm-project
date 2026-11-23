// Under -fsafe-c, main's argv is counted by argc, so the usual signatures
// compile and argv[i] is bounds-checked.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s -DARRAY
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -verify %s
// expected-no-diagnostics

#ifdef ARRAY
int main(int argc, char *argv[]) {
#else
int main(int argc, char **argv) {
#endif
  if (argc < 2)
    return 1;
  return argv[1][0];
}
