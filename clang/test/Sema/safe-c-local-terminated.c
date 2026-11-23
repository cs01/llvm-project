// Under -fsafe-c an unannotated local initialized from a terminated string is
// a terminated string, so 'char *s = strdup(x)' needs no conversion. Written
// annotations still win. Found porting cJSON.
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety \
// RUN:   -fnullability-default=nonnull -include safec.h -verify %s
// RUN: %clang_cc1 -fsyntax-only -include safec.h -verify=plain %s
// plain-no-diagnostics

char *__null_terminated _Nullable dup(const char *__null_terminated s);
void take(char *__null_terminated s);
int len(const char *__null_terminated s);

void f(const char *__null_terminated in) {
  char *_Nullable s = dup(in);
  if (s)
    take(s);
  const char *t = in;
  (void)len(t);
}

void g(const char *__null_terminated in) {
  const char *__bidi_indexable b = in; // expected-error {{requires a linear search for the terminator}} expected-note 2 {{consider using}}
  (void)b;
}
