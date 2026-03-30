/*
 * stdlib.h - Nullability-annotated overlay
 * Wraps the system stdlib.h and re-declares functions with _Nullable/_Nonnull.
 */

#ifndef _NULLSAFE_STDLIB_H
#define _NULLSAFE_STDLIB_H

#include_next <stdlib.h>

#ifdef __clang__

#ifdef __cplusplus
extern "C" {
#endif

void * _Nullable malloc(size_t __size);
void * _Nullable calloc(size_t __nmemb, size_t __size);
void * _Nullable realloc(void * _Nullable __ptr, size_t __size);
void free(void * _Nullable __ptr);

char * _Nullable getenv(const char * _Nonnull __name);

void * _Nullable bsearch(const void * _Nonnull __key,
                         const void * _Nonnull __base,
                         size_t __nmemb,
                         size_t __size,
                         int (* _Nonnull __compar)(const void *, const void *));

#if defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE) || defined(__APPLE__)
char * _Nullable realpath(const char * _Nonnull __restrict __name,
                          char * _Nullable __restrict __resolved);
#endif

#ifdef __cplusplus
}
#endif

#endif /* __clang__ */

#endif /* _NULLSAFE_STDLIB_H */
