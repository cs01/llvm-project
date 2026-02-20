/*
 * stdio.h - Null-safe standard I/O
 * Nullability-annotated C standard library I/O functions
 */

#ifndef _CBANG_STDIO_H
#define _CBANG_STDIO_H

#include <stddef.h>
#include <stdarg.h>

#ifndef _FPOS_T_DECLARED
typedef struct __fpos_t fpos_t;
#define _FPOS_T_DECLARED
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _IO_FILE FILE;

#ifndef EOF
#define EOF (-1)
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

extern FILE* _Nonnull stdin;
extern FILE* _Nonnull stdout;
extern FILE* _Nonnull stderr;

/* File operations */
FILE* _Nullable fopen(const char* _Nonnull __restrict __filename,
                      const char* _Nonnull __restrict __mode);
FILE* _Nullable freopen(const char* _Nullable __restrict __filename,
                        const char* _Nonnull __restrict __mode,
                        FILE* _Nonnull __restrict __stream);
int fclose(FILE* _Nonnull __stream);
int fflush(FILE* _Nullable __stream);

/* Formatted output */
int printf(const char* _Nonnull __restrict __format, ...);
int fprintf(FILE* _Nonnull __restrict __stream,
            const char* _Nonnull __restrict __format, ...);
int sprintf(char* _Nonnull __restrict __s,
            const char* _Nonnull __restrict __format, ...);
int snprintf(char* _Nonnull __restrict __s, size_t __maxlen,
             const char* _Nonnull __restrict __format, ...);
int vprintf(const char* _Nonnull __restrict __format, va_list __arg);
int vfprintf(FILE* _Nonnull __restrict __s,
             const char* _Nonnull __restrict __format, va_list __arg);
int vsprintf(char* _Nonnull __restrict __s,
             const char* _Nonnull __restrict __format, va_list __arg);
int vsnprintf(char* _Nonnull __restrict __s, size_t __maxlen,
              const char* _Nonnull __restrict __format, va_list __arg);

/* Formatted input */
int scanf(const char* _Nonnull __restrict __format, ...);
int fscanf(FILE* _Nonnull __restrict __stream,
           const char* _Nonnull __restrict __format, ...);
int sscanf(const char* _Nonnull __restrict __s,
           const char* _Nonnull __restrict __format, ...);
int vscanf(const char* _Nonnull __restrict __format, va_list __arg);
int vfscanf(FILE* _Nonnull __restrict __s,
            const char* _Nonnull __restrict __format, va_list __arg);
int vsscanf(const char* _Nonnull __restrict __s,
            const char* _Nonnull __restrict __format, va_list __arg);

/* Character I/O */
int fgetc(FILE* _Nonnull __stream);
int getc(FILE* _Nonnull __stream);
int getchar(void);
int fputc(int __c, FILE* _Nonnull __stream);
int putc(int __c, FILE* _Nonnull __stream);
int putchar(int __c);
int ungetc(int __c, FILE* _Nonnull __stream);

/* String I/O */
char* _Nullable fgets(char* _Nonnull __restrict __s, int __n,
                      FILE* _Nonnull __restrict __stream);
int fputs(const char* _Nonnull __restrict __s,
          FILE* _Nonnull __restrict __stream);
int puts(const char* _Nonnull __s);
char* _Nullable gets(char* _Nonnull __s) __attribute__((deprecated));

/* Binary I/O */
size_t fread(void* _Nonnull __restrict __ptr, size_t __size, size_t __nmemb,
             FILE* _Nonnull __restrict __stream);
size_t fwrite(const void* _Nonnull __restrict __ptr, size_t __size,
              size_t __nmemb, FILE* _Nonnull __restrict __stream);

/* File positioning */
int fseek(FILE* _Nonnull __stream, long __offset, int __whence);
long ftell(FILE* _Nonnull __stream);
void rewind(FILE* _Nonnull __stream);
int fgetpos(FILE* _Nonnull __restrict __stream,
            fpos_t* _Nonnull __restrict __pos);
int fsetpos(FILE* _Nonnull __stream, const fpos_t* _Nonnull __pos);

/* Error handling */
void clearerr(FILE* _Nonnull __stream);
int feof(FILE* _Nonnull __stream);
int ferror(FILE* _Nonnull __stream);
void perror(const char* _Nullable __s);

/* File operations */
int remove(const char* _Nonnull __filename);
int rename(const char* _Nonnull __old, const char* _Nonnull __new);
FILE* _Nullable tmpfile(void);
char* _Nullable tmpnam(char* _Nullable __s);

/* Buffering */
void setbuf(FILE* _Nonnull __restrict __stream,
            char* _Nullable __restrict __buf);
int setvbuf(FILE* _Nonnull __restrict __stream,
            char* _Nullable __restrict __buf, int __mode, size_t __size);

#ifdef __cplusplus
}
#endif

#endif /* _CBANG_STDIO_H */
