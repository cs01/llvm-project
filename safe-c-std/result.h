/* A result that is a value or an error, like Rust's Result. Reading the value
   before checking ok is a compile error, and so is a switch on ok that leaves
   a case out.
     typedef RESULT(num, long, enum num_error) num;
     num parse(struct str s) { ... return ERR(num, NUM_EMPTY); ... return OK(num, v); }
     num r = parse(s);
     if (!r.ok) ... r.err ...
     use(r.value);
   TRY(expr, name), like Rust's '?', gives expr's value, or returns its error
   from a function whose result is struct name. It uses a GNU extension, which
   gcc and clang have. */
#ifndef SAFEC_RESULT_H
#define SAFEC_RESULT_H
#include "safec.h"
#include <stdbool.h>

#define RESULT(name, T, E)                                                     \
  struct name {                                                                \
    bool ok;                                                                   \
    union _Tagged_by(ok) {                                                     \
      T value _When(true);                                                     \
      E err _When(false);                                                      \
    };                                                                         \
  }
#define OK(name, v) ((struct name){.ok = true, .value = (v)})
#define ERR(name, e) ((struct name){.ok = false, .err = (e)})
#define TRY(expr, name)                                                        \
  ({                                                                           \
    __typeof__(expr) safec_try_ = (expr);                                      \
    if (!safec_try_.ok)                                                        \
      return ERR(name, safec_try_.err);                                        \
    safec_try_.value;                                                          \
  })
#endif
