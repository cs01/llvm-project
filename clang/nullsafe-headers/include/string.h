/*
 * string.h - Null-safe string operations
 * Nullability-annotated C standard library string functions
 */

#ifndef _CBANG_STRING_H
#define _CBANG_STRING_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* String examination */
size_t strlen(const char* _Nonnull __s);
int strcmp(const char* _Nonnull __s1, const char* _Nonnull __s2);
int strncmp(const char* _Nonnull __s1, const char* _Nonnull __s2, size_t __n);
int strcoll(const char* _Nonnull __s1, const char* _Nonnull __s2);

/* String copying - all return first argument */
char* _Nonnull strcpy(char* _Nonnull __restrict __dest,
                      const char* _Nonnull __restrict __src);
char* _Nonnull strncpy(char* _Nonnull __restrict __dest,
                       const char* _Nonnull __restrict __src,
                       size_t __n);
char* _Nonnull strcat(char* _Nonnull __restrict __dest,
                      const char* _Nonnull __restrict __src);
char* _Nonnull strncat(char* _Nonnull __restrict __dest,
                       const char* _Nonnull __restrict __src,
                       size_t __n);
size_t strxfrm(char* _Nonnull __restrict __dest,
               const char* _Nonnull __restrict __src,
               size_t __n);

/* String searching - return NULL if not found */
char* _Nullable strchr(const char* _Nonnull __s, int __c);
char* _Nullable strrchr(const char* _Nonnull __s, int __c);
char* _Nullable strstr(const char* _Nonnull __haystack,
                       const char* _Nonnull __needle);
char* _Nullable strpbrk(const char* _Nonnull __s,
                        const char* _Nonnull __accept);
size_t strspn(const char* _Nonnull __s, const char* _Nonnull __accept);
size_t strcspn(const char* _Nonnull __s, const char* _Nonnull __reject);

/* String tokenization - first param can be NULL to continue */
char* _Nullable strtok(char* _Nullable __s, const char* _Nonnull __delim);

/* Memory operations - all require non-null, return first argument */
void* _Nonnull memcpy(void* _Nonnull __restrict __dest,
                      const void* _Nonnull __restrict __src,
                      size_t __n);
void* _Nonnull memmove(void* _Nonnull __dest,
                       const void* _Nonnull __src,
                       size_t __n);
void* _Nonnull memset(void* _Nonnull __s, int __c, size_t __n);
int memcmp(const void* _Nonnull __s1, const void* _Nonnull __s2, size_t __n);
void* _Nullable memchr(const void* _Nonnull __s, int __c, size_t __n);

/* Error strings */
char* _Nonnull strerror(int __errnum);

/* POSIX extensions */
#if defined(_POSIX_C_SOURCE) || defined(_GNU_SOURCE)
char* _Nullable strdup(const char* _Nonnull __s);
char* _Nullable strndup(const char* _Nonnull __s, size_t __n);
#endif

#ifdef _GNU_SOURCE
size_t strnlen(const char* _Nonnull __s, size_t __maxlen);
int strcasecmp(const char* _Nonnull __s1, const char* _Nonnull __s2);
int strncasecmp(const char* _Nonnull __s1, const char* _Nonnull __s2, size_t __n);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _CBANG_STRING_H */
