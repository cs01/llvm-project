// Heap arrays of _View structs (2026-11-24): a token array pointing into its
// input, as `Vec<Token<'a>>` in Rust. Before, writes into views through a
// pointer (`toks[i].text = in`) were not tracked, so freeing the input and
// then reading a token compiled. Now a borrow written into views held by a
// local '_Owned' pointer goes to that pointer, which may not be used once
// the input is gone; any other pointer to views is rejected, and pointers to
// views may not be fields or globals.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify=good -DGOOD %s
// good-no-diagnostics
typedef unsigned long size_t;
void *_Nullable malloc(size_t n) __attribute__((ownership_returns(malloc)));
void *_Nullable calloc(size_t c, size_t n) __attribute__((ownership_returns(malloc)));
void free(void *_Nullable p) __attribute__((ownership_takes(malloc, 1)));

struct _View tok { const char *__counted_by(n) text; size_t n; };

_Returns_owned struct tok *_Owned _Nullable lex(const char *__counted_by(1) in _Lifetimebound) {
  struct tok *_Owned _Nullable t = calloc(1, sizeof *t);
  if (!t) return 0;
  t[0].text = in; t[0].n = 1;
  return t;
}

#ifdef GOOD
size_t count(const char *__counted_by(len) in, size_t len) {
  struct tok *_Owned _Nullable toks = calloc(len, sizeof(struct tok));
  if (!toks) return 0;
  for (size_t i = 0; i < len; i++) { toks[i].text = in + i; toks[i].n = 1; }
  size_t s = 0;
  for (size_t i = 0; i < len; i++) s += toks[i].n;
  free(toks);
  return s;
}
char read_then_free(void) {
  char *_Nullable buf = malloc(1);
  if (!buf) return 0;
  buf[0] = 'x';
  struct tok *_Owned _Nullable t = lex(buf);
  char c = t ? t[0].text[0] : 0;
  free(t);
  free(buf);
  return c;
}
#else
char free_input_first(void) {
  char *_Nullable buf = malloc(4); // expected-error {{object allocated by 'malloc' does not live long enough}}
  if (!buf) return 0;
  struct tok *_Owned _Nullable toks = calloc(1, sizeof(struct tok));
  if (!toks) { free(buf); return 0; }
  toks[0].text = buf; toks[0].n = 1; // expected-note {{aliases}}
  free(buf);                         // expected-note {{freed here}}
  char r = toks[0].text[0];          // expected-note {{later used here}}
  free(toks);
  return r;
}
struct holder { struct tok *_Owned _Nullable toks; }; // expected-error {{field 'toks' points to '_View' structs}}
static _Thread_local struct tok *_Owned _Nullable gtoks; // expected-error {{global 'gtoks' points to '_View' structs}}
void through_param(struct tok *_Exclusive v, const char *__counted_by(1) in) {
  v->text = in; v->n = 1; // expected-error {{writing a borrow into a '_View' through a pointer}}
}
void through_alias(const char *__counted_by(1) in) {
  struct tok *_Owned _Nullable t = calloc(1, sizeof *t);
  if (!t) return;
  struct tok *q = t;
  q[0].text = in; q[0].n = 1; // expected-error {{writing a borrow into a '_View' through a pointer}}
  free(t);
}
#endif
