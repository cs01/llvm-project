/*
 * string.h - Nullability-annotated overlay
 * Wraps the system string.h and re-declares functions with _Nullable/_Nonnull.
 */

#ifndef _NULLSAFE_STRING_H
#define _NULLSAFE_STRING_H

#include_next <string.h>

#ifdef __clang__

#ifdef __cplusplus
extern "C" {
#endif

char * _Nullable strchr(const char * _Nonnull __s, int __c);
char * _Nullable strrchr(const char * _Nonnull __s, int __c);
char * _Nullable strstr(const char * _Nonnull __haystack,
                        const char * _Nonnull __needle);
char * _Nullable strpbrk(const char * _Nonnull __s,
                         const char * _Nonnull __accept);
char * _Nullable strtok(char * _Nullable __s, const char * _Nonnull __delim);
void * _Nullable memchr(const void * _Nonnull __s, int __c, size_t __n);

#if defined(_POSIX_C_SOURCE) || defined(_GNU_SOURCE) || defined(__APPLE__)
char * _Nullable strdup(const char * _Nonnull __s);
char * _Nullable strndup(const char * _Nonnull __s, size_t __n);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __clang__ */

#endif /* _NULLSAFE_STRING_H */
