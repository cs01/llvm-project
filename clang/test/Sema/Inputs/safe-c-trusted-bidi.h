#include <ptrcheck.h>
/* Carries its own bounds: not a terminated-string reader, also when called
   from inside the header (once crashed the compiler). */
static inline char first_of(const char *__bidi_indexable s) { return s[0]; }
static inline char first_via(const char *__bidi_indexable s) { return first_of(s); }
