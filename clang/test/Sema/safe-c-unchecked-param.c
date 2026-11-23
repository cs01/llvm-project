// Under -fsafe-c, passing a pointer with bounds to an '__unsafe_indexable'
// parameter is an error: the callee would access it unchecked.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -D__LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fbounds-safety -D__LIBC_STAGED_BOUNDS_SAFETY_ATTRIBUTES -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
int cmp_like(const char *__unsafe_indexable, const char *__unsafe_indexable, size_t);
void free(void *_Nullable __unsafe_indexable);
int strncmp(const char *__unsafe_indexable, const char *__unsafe_indexable, size_t);
char *strncpy_like(char *_Nullable __unsafe_indexable dst, const char *__null_terminated src, size_t n);

__safec_takes(1) void f(char *__counted_by(n) buf, size_t n, char *__unsafe_indexable raw) {
  strncpy_like(buf, "abc", n); // expected-error {{passing 'char *__single __counted_by(n)' (aka 'char *__single') to 'strncpy_like' drops its bounds}}
  strncpy_like(0, "abc", n);
  strncpy_like(raw, "abc", n);
  free(buf); // free never accesses the block through its argument
}

// A C string function stops at the terminator: terminated strings and string
// literals may go to its read-only string parameters; a slice may not.
int g(const char *__null_terminated s, const char *__counted_by(n) slice, size_t n) {
  return strncmp(s, "abc", 3) +
         strncmp(slice, "abc", 3) + // expected-error {{drops its bounds}}
         cmp_like(s, s, 3);     // expected-error 2 {{drops its bounds}}
}
