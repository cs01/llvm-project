// Under -fsafe-c a function that returns memory it allocated must say so
// (ownership_returns), so its callers' frees and later uses are checked. Found
// compiling a cJSON-style example: parse() allocated, the caller freed and
// used the result, and nothing was reported.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

typedef __SIZE_TYPE__ size_t;
void *_Nullable calloc(size_t, size_t) __attribute__((alloc_size(1, 2)));
void free(void *_Nullable);
struct json { int kind; };

struct json *_Nullable parse_untold(void) {
  return calloc(1, sizeof(struct json)); // expected-error {{'parse_untold' returns memory it allocated, but callers are not told}}
}

_Returns_owned struct json *_Nullable parse(void) {
  struct json *_Nullable j = calloc(1, sizeof(struct json));
  return j;
}

__safec_takes(1) void json_delete(struct json *_Nullable j) { free(j); }

int use(void) {
  struct json *_Nullable t = parse(); // expected-error {{object allocated by 'parse' does not live long enough}}
  json_delete(t);                     // expected-note {{freed here}}
  return t ? t->kind : 0;             // expected-note {{later used here}}
}

struct json *_Nullable none(void) { return 0; }
