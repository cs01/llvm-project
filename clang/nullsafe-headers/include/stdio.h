/*
 * stdio.h - Nullability-annotated overlay
 * Wraps the system stdio.h and re-declares functions with _Nullable/_Nonnull.
 */

#ifndef _NULLSAFE_STDIO_H
#define _NULLSAFE_STDIO_H

#include_next <stdio.h>

#ifdef __clang__

#ifdef __cplusplus
extern "C" {
#endif

FILE * _Nullable fopen(const char * _Nonnull __restrict __filename,
                       const char * _Nonnull __restrict __mode);
FILE * _Nullable freopen(const char * _Nullable __restrict __filename,
                         const char * _Nonnull __restrict __mode,
                         FILE * _Nonnull __restrict __stream);
FILE * _Nullable tmpfile(void);
char * _Nullable tmpnam(char * _Nullable __s);
char * _Nullable fgets(char * _Nonnull __restrict __s, int __n,
                       FILE * _Nonnull __restrict __stream);

#if defined(_POSIX_C_SOURCE) || defined(__APPLE__)
FILE * _Nullable popen(const char * _Nonnull __command,
                       const char * _Nonnull __type);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __clang__ */

#endif /* _NULLSAFE_STDIO_H */
