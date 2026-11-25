// A counted field read through a pointer that was checked for null: the count
// is read through the same pointer (this crashed, with no place to report).
// RUN: %clang_cc1 -fsyntax-only -fsafe-c -fbounds-safety -fnullability-safety -fnullability-default=nonnull -verify %s
struct q { unsigned long n; int *_Nullable __attribute__((__counted_by__(n))) d; };
int checked(struct q *_Nullable a) {
  if (!a)
    return 1;
  int *_Nullable b = a->d;
  return b ? b[1] : 0;
}
int unchecked(struct q *_Nullable a) {
  int *_Nullable b = a->d; // expected-error 1+ {{dereference of nullable pointer}} expected-note 1+ {{}}
  return b ? b[1] : 0;
}
