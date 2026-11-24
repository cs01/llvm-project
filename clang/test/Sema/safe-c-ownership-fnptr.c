// Under -fsafe-c, ownership_takes on a function pointer (a field or variable)
// makes calls through it free their argument for the lifetime analysis; a
// function that frees may be stored only into a pointer declared to free.
// RUN: %clang_cc1 -fsyntax-only -mllvm -safe-c-shared-globals=false -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable malloc(size_t) __attribute__((alloc_size(1)));
void free(void *_Nullable);

struct hooks {
  void (*dealloc)(void *_Nullable) _Takes(1);
  void (*log)(void *_Nullable);
};

_Takes(1) void my_free(void *_Nullable p) { free(p); }
void my_log(void *_Nullable p) { (void)p; }

struct hooks good = {my_free, my_log};
struct hooks bad = {my_log, my_free}; // expected-error {{'my_free' frees its parameter 1, but 'log' does not say so}}
struct hooks with_libc = {free, free}; // expected-error {{'free' frees its parameter 1, but 'log' does not say so}}

void assign(struct hooks *h) {
  h->dealloc = free;
  h->log = free; // expected-error {{'free' frees its parameter 1, but 'log' does not say so}}
}

int use_after(struct hooks *h) {
  int *_Nullable p = malloc(sizeof(int)); // expected-error {{does not live long enough}}
  h->dealloc(p);                          // expected-note {{freed here}}
  return p ? *p : 0;                      // expected-note {{later used here}}
}

// Freeing through the pointer counts as freeing the parameter.
void release(struct hooks *h, void *_Nullable p) {
  h->dealloc(p); // expected-error {{'release' frees its parameter 'p'}}
}
