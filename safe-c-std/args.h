/* Command-line options, by position in argv. 0 means not there. */
#ifndef SAFEC_ARGS_H
#define SAFEC_ARGS_H
#include "text.h"
#if defined(__clang__)
#pragma clang assume_nonnull begin
#endif

/* Whether it is options ('-x', not '-' or a file). */
static inline int args_flags(const char *__null_terminated a) {
  if (*a != '-')
    return 0;
  a++;
  return *a != 0;
}

/* Where 'name' is. */
static inline int args_find(int argc, char *__null_terminated _Static const *__counted_by(argc) argv,
                            const char *__null_terminated name) {
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], name) == 0)
      return i;
  }
  return 0;
}

/* Where the value after 'name' is: argv[args_value(argc, argv, "--out")]. */
static inline int args_value(int argc, char *__null_terminated _Static const *__counted_by(argc) argv,
                             const char *__null_terminated name) {
  int i = args_find(argc, argv, name);
  return i > 0 && i + 1 < argc ? i + 1 : 0;
}

/* The number after 'name': 1 and *out, or 0 if it is missing or not a number. */
static inline int args_int(int argc, char *__null_terminated _Static const *__counted_by(argc) argv,
                           const char *__null_terminated name, long *out) {
  int i = args_value(argc, argv, name);
  if (i == 0)
    return 0;
  size_t n = strlen(argv[i]);
  struct str s = {__null_terminated_to_indexable(argv[i]), n};
  return str_int(s, out);
}

#if defined(__clang__)
#pragma clang assume_nonnull end
#endif
#endif
