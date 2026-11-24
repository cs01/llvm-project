/* Safe C port helpers for NIST Juliet, included after std_testcase.h by
 * juliet-port.ts. The C library's strcpy family is unavailable under bounds
 * safety (it cannot see the destination's size). These replacements copy
 * through bounds-checked pointers, so a copy past the end of the destination,
 * or a read past the end of the source, traps: the bug is still caught, not
 * hidden (strlcpy would truncate instead). Parameters are _Nonnull, as the
 * C library's are (C17 7.1.4): this header is included as a system header,
 * where unannotated pointers would not be checked for null. */
#ifndef JULIET_PORT_H
#define JULIET_PORT_H
#include <stddef.h>
#include <wchar.h>

static inline char *juliet_strcpy(char *_Nonnull __bidi_indexable d,
                                  const char *_Nonnull __bidi_indexable s) {
  for (size_t i = 0;; i++) {
    d[i] = s[i];
    if (!s[i])
      return d;
  }
}
static inline char *juliet_strcat(char *_Nonnull __bidi_indexable d,
                                  const char *_Nonnull __bidi_indexable s) {
  size_t n = 0;
  while (d[n])
    n++;
  juliet_strcpy(d + n, s);
  return d;
}
static inline wchar_t *juliet_wcscpy(wchar_t *_Nonnull __bidi_indexable d,
                                     const wchar_t *_Nonnull __bidi_indexable s) {
  for (size_t i = 0;; i++) {
    d[i] = s[i];
    if (!s[i])
      return d;
  }
}
static inline wchar_t *juliet_wcscat(wchar_t *_Nonnull __bidi_indexable d,
                                     const wchar_t *_Nonnull __bidi_indexable s) {
  size_t n = 0;
  while (d[n])
    n++;
  juliet_wcscpy(d + n, s);
  return d;
}
static inline char *juliet_strncat(char *_Nonnull __bidi_indexable d,
                                   const char *_Nonnull __bidi_indexable s, size_t n) {
  size_t e = 0;
  while (d[e])
    e++;
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[e + i] = s[i];
  d[e + i] = 0;
  return d;
}
static inline wchar_t *juliet_wcsncat(wchar_t *_Nonnull __bidi_indexable d,
                                      const wchar_t *_Nonnull __bidi_indexable s,
                                      size_t n) {
  size_t e = 0;
  while (d[e])
    e++;
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[e + i] = s[i];
  d[e + i] = 0;
  return d;
}
static inline char *juliet_strncpy(char *_Nonnull __bidi_indexable d,
                                   const char *_Nonnull __bidi_indexable s,
                                   size_t n) {
  /* Exactly n bytes, zero-padded past the end of s, as C's strncpy. */
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
  return d;
}
static inline wchar_t *juliet_wcsncpy(wchar_t *_Nonnull __bidi_indexable d,
                                      const wchar_t *_Nonnull __bidi_indexable s,
                                      size_t n) {
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[i] = s[i];
  for (; i < n; i++)
    d[i] = 0;
  return d;
}
/* io.c's flags are set once and never written, but live in another file, so
 * the compiler cannot see which branch of `if (globalTrue)` runs. A ported
 * project would make them constants in the header; their values are io.c's,
 * so every branch is taken exactly as before. */
#ifndef JULIET_PORT_NO_CONST_FLAGS
#define GLOBAL_CONST_TRUE 1
#define GLOBAL_CONST_FALSE 0
#define GLOBAL_CONST_FIVE 5
#define globalTrue 1
#define globalFalse 0
#define globalFive 5
#endif
#define strcpy(d, s) juliet_strcpy((d), (s))
#define strncpy(d, s, n) juliet_strncpy((d), (s), (n))
#define wcsncpy(d, s, n) juliet_wcsncpy((d), (s), (n))
#define strncat(d, s, n) juliet_strncat((d), (s), (n))
#define wcsncat(d, s, n) juliet_wcsncat((d), (s), (n))
#define strcat(d, s) juliet_strcat((d), (s))
#define wcscpy(d, s) juliet_wcscpy((d), (s))
#define wcscat(d, s) juliet_wcscat((d), (s))
#endif
