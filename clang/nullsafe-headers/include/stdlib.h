/*
 * stdlib.h - Null-safe standard library
 * Nullability-annotated C standard library utility functions
 */

#ifndef _CBANG_STDLIB_H
#define _CBANG_STDLIB_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory allocation - all return NULL on failure */
void* _Nullable malloc(size_t __size);
void* _Nullable calloc(size_t __nmemb, size_t __size);
void* _Nullable realloc(void* _Nullable __ptr, size_t __size);
void free(void* _Nullable __ptr);

/* String conversion */
int atoi(const char* _Nonnull __nptr);
long atol(const char* _Nonnull __nptr);
long long atoll(const char* _Nonnull __nptr);
double atof(const char* _Nonnull __nptr);

long strtol(const char* _Nonnull __restrict __nptr,
            char* _Nullable * _Nullable __restrict __endptr,
            int __base);
unsigned long strtoul(const char* _Nonnull __restrict __nptr,
                      char* _Nullable * _Nullable __restrict __endptr,
                      int __base);
long long strtoll(const char* _Nonnull __restrict __nptr,
                  char* _Nullable * _Nullable __restrict __endptr,
                  int __base);
unsigned long long strtoull(const char* _Nonnull __restrict __nptr,
                            char* _Nullable * _Nullable __restrict __endptr,
                            int __base);
double strtod(const char* _Nonnull __restrict __nptr,
              char* _Nullable * _Nullable __restrict __endptr);
float strtof(const char* _Nonnull __restrict __nptr,
             char* _Nullable * _Nullable __restrict __endptr);

/* Random numbers */
int rand(void);
void srand(unsigned int __seed);

/* Environment */
char* _Nullable getenv(const char* _Nonnull __name);
int system(const char* _Nullable __command);

/* Program termination */
_Noreturn void exit(int __status);
_Noreturn void abort(void);
int atexit(void (* _Nonnull __func)(void));

/* Searching and sorting */
void* _Nullable bsearch(const void* _Nonnull __key,
                        const void* _Nonnull __base,
                        size_t __nmemb,
                        size_t __size,
                        int (* _Nonnull __compar)(const void* _Nonnull,
                                                   const void* _Nonnull));
void qsort(void* _Nonnull __base,
           size_t __nmemb,
           size_t __size,
           int (* _Nonnull __compar)(const void* _Nonnull,
                                      const void* _Nonnull));

/* Integer arithmetic */
int abs(int __x);
long labs(long __x);
long long llabs(long long __x);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
typedef struct { long long quot; long long rem; } lldiv_t;

div_t div(int __numer, int __denom);
ldiv_t ldiv(long __numer, long __denom);
lldiv_t lldiv(long long __numer, long long __denom);

/* Multibyte/wide character conversions */
int mblen(const char* _Nullable __s, size_t __n);
int mbtowc(wchar_t* _Nullable __restrict __pwc,
           const char* _Nullable __restrict __s,
           size_t __n);
int wctomb(char* _Nullable __s, wchar_t __wc);
size_t mbstowcs(wchar_t* _Nullable __restrict __pwcs,
                const char* _Nullable __restrict __s,
                size_t __n);
size_t wcstombs(char* _Nullable __restrict __s,
                const wchar_t* _Nullable __restrict __pwcs,
                size_t __n);

/* POSIX extensions */
#if defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)
char* _Nullable mktemp(char* _Nonnull __template);
int mkstemp(char* _Nonnull __template);
char* _Nullable realpath(const char* _Nonnull __restrict __name,
                         char* _Nullable __restrict __resolved);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _CBANG_STDLIB_H */
