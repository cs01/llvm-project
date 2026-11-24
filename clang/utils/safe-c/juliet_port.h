/* Safe C port helpers for NIST Juliet, included after std_testcase.h by
 * juliet-port.ts. The C library's strcpy family is unavailable under bounds
 * safety (it cannot see the destination's size). These replacements copy
 * through bounds-checked pointers, so a copy past the end of the destination,
 * or a read past the end of the source, traps: the bug is still caught, not
 * hidden (strlcpy would truncate instead). */
#ifndef JULIET_PORT_H
#define JULIET_PORT_H
#include <stddef.h>
#include <wchar.h>

static inline char *juliet_strcpy(char *__bidi_indexable d,
                                  const char *__bidi_indexable s) {
  for (size_t i = 0;; i++) {
    d[i] = s[i];
    if (!s[i])
      return d;
  }
}
static inline char *juliet_strcat(char *__bidi_indexable d,
                                  const char *__bidi_indexable s) {
  size_t n = 0;
  while (d[n])
    n++;
  juliet_strcpy(d + n, s);
  return d;
}
static inline wchar_t *juliet_wcscpy(wchar_t *__bidi_indexable d,
                                     const wchar_t *__bidi_indexable s) {
  for (size_t i = 0;; i++) {
    d[i] = s[i];
    if (!s[i])
      return d;
  }
}
static inline wchar_t *juliet_wcscat(wchar_t *__bidi_indexable d,
                                     const wchar_t *__bidi_indexable s) {
  size_t n = 0;
  while (d[n])
    n++;
  juliet_wcscpy(d + n, s);
  return d;
}
static inline char *juliet_strncat(char *__bidi_indexable d,
                                   const char *__bidi_indexable s, size_t n) {
  size_t e = 0;
  while (d[e])
    e++;
  size_t i = 0;
  for (; i < n && s[i]; i++)
    d[e + i] = s[i];
  d[e + i] = 0;
  return d;
}
static inline wchar_t *juliet_wcsncat(wchar_t *__bidi_indexable d,
                                      const wchar_t *__bidi_indexable s,
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
#define strcpy(d, s) juliet_strcpy((d), (s))
#define strncat(d, s, n) juliet_strncat((d), (s), (n))
#define wcsncat(d, s, n) juliet_wcsncat((d), (s), (n))
#define strcat(d, s) juliet_strcat((d), (s))
#define wcscpy(d, s) juliet_wcscpy((d), (s))
#define wcscat(d, s) juliet_wcscat((d), (s))
#endif
